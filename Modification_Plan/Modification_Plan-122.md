# 架构审计方案：卡片数据渲染代理多套/多种方式冗余架构审计排查 (Modification_Plan-122.md)

## 1. 冗余及设计硬伤根源分析

通过对整个 `src/ui/` 目录下相关 Delegate 的静态审计与排查，确认在数据层渲染和卡片展示控制链中，确实并存了**三套互不关联、高度冗余、重复实现**的绘制与逻辑委托，其职责重叠与架构缺陷极其严重。

以下为核心的冗余架构硬伤及代码证据：

### 1.1 `ThumbnailDelegate` 与 `GridItemDelegate` 的高度冗余
- **物理现状**：
  在 `ContentPanel::initGridView` 方法（`src/ui/ContentPanel.cpp`）中：
  - 当视图是 `JustifiedView`（合理自适应对齐视图）时，使用的是 `ThumbnailDelegate`：
    ```cpp
    auto* delegate = new ThumbnailDelegate(this);
    m_gridView->setItemDelegate(delegate);
    ```
  - 当视图是其他 GridResult 视图时，使用的是定义在 `ContentPanel.h/cpp` 内的 `GridItemDelegate`：
    ```cpp
    m_gridView->setItemDelegate(new GridItemDelegate(this));
    ```
- **职责重叠**：
  这两个 Delegate 独立计算了几乎一样的卡片度量参数（`Metrics` vs `GridMetrics`）、独立手写了星级绘制（`drawStar`）、状态图标绘制（`ban_icon`）、Pin图标、标签背景、颜色圆点叠加等全部的卡片装饰逻辑。
  - `ThumbnailDelegate::paint` 与 `GridItemDelegate::paint` 都分别从 `index.data(...)` 中频繁读取 `RatingRole`, `PinnedRole`, `ManagedRole`, `ColorRole` 等自定义元数据角色，导致了绘制逻辑的双头马车。

### 1.2 `TreeItemDelegate` 职责过载与逻辑二次重复
- **物理现状**：
  在树状结果视图 `m_treeView` 初始化中：
  ```cpp
  m_treeView->setItemDelegate(new TreeItemDelegate(this, true, true));
  ```
- **职责重叠**：
  在 `TreeItemDelegate::paint` 的 `col == 0 && m_drawMiniCards` 以及 `col == 1`、`col == 2` 分支中，再次大量手写了针对微型卡片（最左侧看片）下的缩略图裁切绘制、系统关联大图标提取绘制、PIN 垂直小图标、数据导入进度环计算、以及星级 rating 隐藏展示逻辑。
  - 同一套星级和进度的渲染公式在三个不同的 Delegate（`ThumbnailDelegate`, `GridItemDelegate`, `TreeItemDelegate`）里各自独立复制粘贴了一份，产生了极高的一致性维护成本，属于非常经典的冗余和傻逼架构。

---

## 2. 修改边界声明【范围】

本案属于**分析师角色**发起的静态架构审计与重构规划，不进行任何物理代码文件（`.cpp`、`.h`）的物理修改或重构，不更改项目构建流程。

### 物理文件审计清单（只读分析）：
1. `src/ui/ThumbnailDelegate.cpp` / `src/ui/ThumbnailDelegate.h`
2. `src/ui/ContentPanel.cpp` 中的 `GridItemDelegate` 模块定义
3. `src/ui/TreeItemDelegate.h`

---

## 3. 建议的重构与整改设计

为了彻底清除卡片渲染层的多套冗余，未来在 **执行者角色** 下应当推进如下工业级极致重构方案：

### 3.1 引入单一渲染核心 `CardPainterHelper`
- **重构设想**：
  将 `ThumbnailDelegate`、`GridItemDelegate` 以及 `TreeItemDelegate` 中的视觉要素（例如：圆角边框绘制、高光叠加、星级星星、同步状态圆环、Pin角标、甚至缩略图适配）全部抽离到一个非代理的纯静态绘制助手 `CardPainterHelper` 中。
- **职责收敛**：
  三个 Delegate 不再保留任何具体的画笔（QPainter）绘图和路径坐标计算细节，仅负责事件分发、编辑器状态（如 F2 行内编辑双向同步、QLineEdit）和传递对应的 QRect 布局视口给 `CardPainterHelper`。这能将卡片渲染归一化，从根本上杜绝“多套卡片、风格不一、代码冗余”的架构硬伤。
