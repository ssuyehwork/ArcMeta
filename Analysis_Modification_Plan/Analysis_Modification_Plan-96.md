# 侧边栏分类面板增加内置搜索过滤功能 —— Analysis_Modification_Plan-96.md

## 1. 任务背景
在侧边栏“分类”面板底部集成搜索过滤功能。要求 1:1 参照系统既有输入框标准（内置清除按钮与图标），实现实时树形过滤及匹配项文字高亮，并包含右侧功能按钮。

## 2. 问题定位
- **布局调整**：`src/ui/CategoryPanel.cpp` 的 `initUi`。需在 `m_categoryTree` 下方新增水平布局容器，且严禁添加分割线。
- **架构升级**：需引入 `QSortFilterProxyModel` 子类包装 `CategoryModel`，处理树形递归显隐逻辑。
- **渲染增强**：`src/ui/CategoryDelegate.h` 需感知搜索关键词并执行 `PrimaryBlue` 文字着色。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 其他输入框的“×”都在输入框里 | 调用 `setClearButtonEnabled(true)` 且复用 `MainWindow` 样式 | ✅ |
| 2    | 图标应为方框打钩图标 | 使用 `select` 图标并通过 `addAction(..., LeadingPosition)` 嵌入 | ✅ |
| 3    | 为什么还出现多余的分割线 | 彻底移除 `border-top: 1px solid #333` 设计 | ✅ |
| 4    | 匹配到的分类名称显示高亮 | `CategoryDelegate` 在绘制时对匹配子串应用 `PrimaryBlue` | ✅ |
| 5    | 搜索框右侧独立按钮 | 在布局右侧添加 `setFixedSize(24, 24)` 的按钮 | ✅ |

## 4. 详细解决方案

### 4.1 代码考古与复用声明 (Code Archaeology)
- **输入框标准**：参考 `src/ui/MainWindow.cpp` 第 890 行。
  - *复用代码*：`m_searchEdit->setClearButtonEnabled(true)`；`m_searchEdit->addAction(UiHelper::getIcon("select", ...), QLineEdit::LeadingPosition)`。
- **按钮布局标准**：参考 `src/ui/AddressBar.cpp` 第 43 行。
  - *复用模式*：使用 `QHBoxLayout` 紧邻排列，设置 4-8px 边距，无背景边框干扰。

### 4.2 UI 实现 (CategoryPanel.cpp)
```cpp
// 1. 构造容器（移除分割线，维持 0 边距对齐）
QWidget* bottomBar = new QWidget(this);
QHBoxLayout* barLayout = new QHBoxLayout(bottomBar);
barLayout->setContentsMargins(8, 4, 8, 8);
barLayout->setSpacing(5);

// 2. 搜索框逻辑（1:1 复刻 MainWindow）
m_searchEdit = new QLineEdit(bottomBar);
m_searchEdit->setPlaceholderText("搜索分类...");
m_searchEdit->setClearButtonEnabled(true);
m_searchEdit->addAction(UiHelper::getIcon("select", TextMuted, 14), QLineEdit::LeadingPosition);
m_searchEdit->setStyleSheet(QString(R"(
    QLineEdit { 
        background: %1; border: 1px solid %2; border-radius: 6px; 
        color: %3; padding-left: 5px; font-size: 12px; height: 26px;
    }
    QLineEdit:focus { border: 1px solid %4; }
)").arg(qssColor(BackgroundDeep), qssColor(BorderColor), qssColor(TextMain), qssColor(PrimaryBlue)));

// 3. 右侧独立按钮
QPushButton* btnAction = new QPushButton(bottomBar);
btnAction->setFixedSize(24, 24);
btnAction->setIcon(UiHelper::getIcon("menu_dots", TextMuted, 16));
btnAction->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; }");
```

### 4.3 递归过滤模型 (CategoryFilterProxyModel)
自定义 `QSortFilterProxyModel` 并重写 `filterAcceptsRow`：
- 若当前项名称匹配，返回 `true`。
- 若当前项不匹配，但其**任何递归子项**匹配，则当前项必须返回 `true` 以维持树形路径。

### 4.4 渲染逻辑 (CategoryDelegate.h)
- 在 `paint` 中，若 `m_searchKeyword` 非空，使用 `painter->drawText` 配合 `fontMetrics`。
- 采用双段或三段绘制法：`[未匹配前缀]` + `[蓝色匹配段]` + `[未匹配后缀]`。

## 5. 修改边界声明【红线】
**本次方案涉及范围：**
- [ ] `src/ui/CategoryPanel.h/cpp`：新增布局、ProxyModel 变量、索引映射逻辑。
- [ ] `src/ui/CategoryDelegate.h`：新增高亮绘制代码。

**明确禁止越界修改的范围：**
- [ ] 严禁在 `CategoryPanel` 顶部或其他面板新增搜索逻辑。
- [ ] 严禁修改 `CategoryModel::data` 返回的原始原始字符串（必须在 View 层高亮）。

## 6. 实现准则与预警【核心】
1. **索引映射（致命点）**：必须确保所有 `clicked`、`expanded` 信号中的 `index` 经由 `m_proxyModel->mapToSource(index)` 转换后再传递给 `CategoryRepo` 或处理 ID 逻辑。
2. **文本计算**：高亮渲染时需考虑 `itemText` 后缀包含的计数器 `(n)` 占位符，避免将计数器误识别为关键词。
3. **头文件依赖**：需增加 `#include <QSortFilterProxyModel>`。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除 | 一律使用原生 setClearButtonEnabled | ✅ |
| 标题栏按钮 | 严禁使用 rgba 蒙版 | ✅ |
| 样式常数 | 复用 StyleLibrary 中的 PrimaryBlue 等 | ✅ |

## 8. 待确认事项
- 右侧独立按钮的具体功能逻辑（方案暂定为预留 UI 占位及通用菜单按钮）。
