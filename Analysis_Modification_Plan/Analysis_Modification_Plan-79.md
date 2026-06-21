# 标题栏新增品牌 Logo 功能实现 —— Analysis_Modification_Plan-79.md

## 1. 任务背景
为了强化品牌识别度，用户期望在 `MainWindow` 标题栏的最左侧添加应用程序 Logo（`ferrex.svg`）。目前标题栏仅显示 "FERREX" 文本标识。

## 2. 问题定位

### 2.1 现状分析
1.  **UI 缺失**：`m_titleBarWidget` 的布局中目前直接以 `m_appNameLabel` ("FERREX") 开头。
2.  **资源可用性**：`src/ui/SvgIcons.h` 中已包含 `ferrex` 图标资源。
3.  **视觉规范**：标题栏高度为 34px，现有文字大小为 12px。Logo 需在此范围内实现垂直居中且比例协调。

### 2.2 涉及文件
- `src/ui/MainWindow.h`：新增 `m_logoLabel` 成员。
- `src/ui/MainWindow.cpp`：在 `setupSplitters` 中实现 Logo 的创建与插入。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在 mainwindow 的标题栏最左侧添加 logo | 在 `m_titleBarLayout` 的 index 0 位置插入 `m_logoLabel` | ✅ |
| 2    | logo 资源为 ferrex.svg | 使用 `UiHelper::getIcon("ferrex", ...)` | ✅ |

## 4. 详细解决方案

### 4.1 修改 MainWindow.h
在 `private` 部分新增成员变量声明：
```cpp
QLabel* m_logoLabel = nullptr;
```

### 4.2 修改 MainWindow.cpp
在 `setupSplitters()` 函数中，在创建 `m_titleBarLayout` 后，插入以下初始化逻辑：

```cpp
// src/ui/MainWindow.cpp

// --- 1. 自定义标题栏 (第一行) ---
// ... 
m_titleBarLayout = new QHBoxLayout(m_titleBarWidget);
m_titleBarLayout->setContentsMargins(5, 0, kEdgeMargin, 0); 
m_titleBarLayout->setSpacing(8); // 适当微调间距以确保视觉平衡

// [新增] Logo 初始化
m_logoLabel = new QLabel(m_titleBarWidget);
// 视觉修正：ferrex.svg 包含微小的透明边距，设置 16x16 渲染尺寸配合适当布局以确保正圆/正方感
m_logoLabel->setFixedSize(18, 18); 
m_logoLabel->setPixmap(UiHelper::getIcon("ferrex", BrandOrange).pixmap(16, 16));
m_logoLabel->setAlignment(Qt::AlignCenter);
m_logoLabel->setStyleSheet("background: transparent; border: none;");
m_titleBarLayout->addWidget(m_logoLabel);

// 文字颜色对齐
m_appNameLabel = new QLabel("FERREX", m_titleBarWidget);
m_appNameLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(BrandOrange.name()));
m_titleBarLayout->addWidget(m_appNameLabel);
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow` 的标题栏布局构造与样式初始化。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `ferrex.svg` 的原始矢量路径。
- [ ] 禁止单独修改 Logo 或文字中的某一方颜色。

## 6. 实现准则与预警【核心】

1.  **颜色一致性**：必须统一使用 `ArcMeta::Style::BrandOrange` (#FF551C)。严禁 Logo 使用橙色而文字使用灰色。
2.  **视觉正方性**：`ferrex.svg` 在渲染时若出现拉伸，需检查 `QLabel` 的 `scaledContents` 属性（应保持默认 false）或 `pixmap` 尺寸是否与 `fixedSize` 产生冲突。
3.  **布局层级**：确保 Logo 位于 `m_titleBarLayout` 的 index 0，文字位于 index 1。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 标题栏规范 | 高度 32px (注：代码中为 34px), 边距 5px | ✅ 方案遵循现有布局代码的物理参数 |
| 按钮/图标标准 | 按钮 24x24px，图标 16x16px | ⚠️ Logo 作为非交互装饰元素，采用 18x18px 以增强品牌感 |

## 8. 待确认事项
- 无。
