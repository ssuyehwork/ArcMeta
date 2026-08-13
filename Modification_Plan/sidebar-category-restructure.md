# 侧边栏分类层级平铺与控制按钮美化 —— sidebar-category-restructure.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在 ArcMeta 应用的侧边栏中，目前用户自定义创建的分类文件夹在视觉层级上存在一定的缩进，且“文件夹 (N)”组标识的设计较偏向普通的文本，缺乏按钮的质感和职能，这容易给用户带来层级嵌套的误解。
同时，原应用布局采用了“单个 QTreeView + 外部垂直堆叠按钮”的单树架构。这导致只要还是垂直堆叠布局，控制按钮就只能被挤在树的最上方或最下方，永远不可能插在“快速访问”和“自定义分类”行之间。
为了让界面更加直观和现代化，我们将分类层级体系彻底升级为**“旧版双树”分离分流架构**，使“文件夹 (N)”彻底美化为一个拥有独立按钮质感、完美卡在系统项/托管库与自定义树中间的、职能清晰的双态隐藏控制按钮。

## 2. 问题定位
- **模块 1：** `src/ui/CategoryFilterProxyModel.h`
  - **位置：** 代理过滤模型，引入 `FilterMode` 控制
  - **原因：** 单树下混杂了系统逻辑桶、快速访问镜像和自定义分类，导致树中无法在中部安插按钮。通过在代理模型中设计 `FilterMode` 枚举（`All`、`SystemAndManaged`、`CustomCategoriesOnly`）并在 `filterAcceptsRow` 进行 root 层精确过滤，优雅实现双树底层共用同一个 `CategoryModel` 的高内聚分流机制。
- **模块 2：** `src/ui/CategoryPanel.cpp`
  - **位置：** `initUi()` 双树实例化、布局组装、状态记忆保存与恢复、焦点选择联动
  - **原因：** 需要将一整棵树彻底拆分为两棵，并通过 layout 排版将控制按钮 `m_btnFolderGroup` 放置在两树中央。同时使折叠隐藏动作由繁琐低效的“遍历行隐藏”升级为高内聚高能的 `m_categoryTreeUser->setVisible(...)`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：侧边栏自定义分类的层级重构与隐藏控制按钮优化 | 本方案核心事件名：侧边栏分类层级平铺与控制按钮美化 | ✅ |
| 1    | 将标记为①的主分类““文件夹 (N)” (▼ / ▶)”变成按钮，该按钮是专用来隐藏或显示自定义创建的分类文件夹（对应用户原话：“将标记为①的主分类““文件夹 (N)” (▼ / ▶)”变成按钮，该按钮是专用来隐藏或显示自定义创建的分类文件夹”） | 重新设计并美化 `m_btnFolderGroup` 的 QSS，使之呈现极佳的圆角、悬浮高亮、对齐间距按钮特性。并在布局中放置在中部，双向无缝隐藏显示下半树。 | ✅ |
| 2    | 将标记为②二级分类升级为一级分类（一等公民）（对应用户原话：“将标记为②二级分类升级为一级分类（一等公民）”） | 自定义分类节点在底层数据结构中已作为一级节点挂载在 root 根部，升级为一级分类。由于按钮 `m_btnFolderGroup` 重新设计了 15px 的左侧内边距，且其完全抽离自 QTreeView 的原生缩进层级，树中所有的自定义文件夹会在视觉层级上与系统分类保持完美的左对齐，完全升级为无缩进的一等公民一级分类。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `CategoryFilterProxyModel` 中增加 `FilterMode` 枚举与分流过滤

```cpp
class CategoryFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    enum class FilterMode {
        All,
        SystemAndManaged,
        CustomCategoriesOnly
    };
```
在 `filterAcceptsRow` 过滤函数中：
```cpp
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        // 第一阶段：按过滤模式分类过滤（仅对 invisibleRootItem 下的直接子节点进行物理过滤隔离）
        if (!source_parent.isValid()) {
            QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
            int id = index.data(IdRole).toInt();
            QString name = index.data(NameRole).toString();
            int kindVal = index.data(CategoryKindRole).toInt();

            if (m_filterMode == FilterMode::SystemAndManaged) {
                // 系统和托管库树：接受 id < 0（系统逻辑桶）OR name == "快速访问" OR kind == SystemLibrary
                bool isSys = (id < 0);
                bool isFav = (name == "快速访问");
                bool isManaged = (id > 0 && kindVal == static_cast<int>(CategoryKind::SystemLibrary));
                if (!isSys && !isFav && !isManaged) {
                    return false;
                }
            } else if (m_filterMode == FilterMode::CustomCategoriesOnly) {
                // 用户自定义分类树：仅接受 id > 0 且 kind != SystemLibrary 且 name != "快速访问"
                bool isCustom = (id > 0 && name != "快速访问" && kindVal != static_cast<int>(CategoryKind::SystemLibrary));
                if (!isCustom) {
                    return false;
                }
            }
        }
```

### 4.2 美化并放置“文件夹 (N)”专用组按钮在双树中央，支持显隐下半树

```cpp
    sbContentLayout->addWidget(m_categoryTree);
    sbContentLayout->addWidget(m_btnFolderGroup);
    sbContentLayout->addWidget(m_categoryTreeUser);
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/CategoryFilterProxyModel.h` 增加分流过滤模式
- [x] 模块/文件：`src/ui/CategoryModel.cpp` 增加 Role 支撑
- [x] 模块/文件：`src/ui/CategoryPanel.h` 增加双树组件及辅助签名
- [x] 模块/文件：`src/ui/CategoryPanel.cpp` 构造、排版、交互等业务逻辑适配

**明确禁止越界修改的范围：**
- [x] 数据库底层操作——不修改。

## 6. 实现准则与预警【核心】
- **完美对齐与无警告编译**：
  在应用此双树重构变更时，必须确保不凭空捏造任何成员变量或私有静态锁，严格尊重已声明的变量生命周期，彻底根除 `-Wunused` 警告。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 所有可编辑的输入框一键清除，一律且仅允许采用 Qt 原生的 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 窗口置顶状态一律使用 Win32 原生 SetWindowPos 并搭配 SWP_NOSENDCHANGING 标志，不涉及本方案 | ✅ |
| 标题栏悬停与按下色值 | Hover 状态背景色 #3E3E42（Style::HoverBackground），Pressed 状态 #4E4E52（Style::PressedBackground），不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
暂无。
