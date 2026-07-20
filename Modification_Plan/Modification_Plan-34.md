# 自适应、网格、列表三大模式深度解耦与长效技术隔离方案 —— Modification_Plan-34.md

## 1. 任务背景
在项目历史的多次迭代中，由于网格视图（`GridMode`）、等高自适应拼图视图（`JustifiedMode`）以及列表视图（`ListView`）存在着绘制代理（Delegate）和视图排版（View Layout）上的逻辑共性，在重构或修复某一视图（如网格 thumbnail 不拉伸）时，经常因为常数冲突、属性未更新、或双击坐标计算未解耦，而引发另外两个模式的退化和错位（顾此失彼）。
本方案旨在为这三种模式设计一套**长效的技术隔离与长治久安方案**，从“布局模式动态同步、绘制几何常数与双击判定、列表高度解耦”三个关键技术维度实现彻底解耦与互不干扰。

## 2. 核心隔离逻辑设计（三大支柱）

### 支柱一：基于 `QObject` 动态属性的状态硬隔离
* **方案内容**：
  在 `JustifiedView` 控件中，通过切换 `LayoutMode` 时，向视图自身注入并强制更新 `"gridMode"` 这一强契约状态属性。
  ```cpp
  void JustifiedView::setLayoutMode(LayoutMode mode) {
      if (m_layoutMode != mode) {
          m_layoutMode = mode;
          setProperty("gridMode", m_layoutMode == GridMode); // 隔离桥梁
          scheduleLayout();
      }
  }
  ```
* **如何防范干扰**：
  - **在网格模式下**：`gridMode` 为 `true` ➡️ 使得 `ThumbnailDelegate` 只能在网格卡片里触发 `Qt::KeepAspectRatio`（等比不拉伸容纳）。
  - **在自适应模式下**：`gridMode` 自动更新为 `false` ➡️ 使得 `ThumbnailDelegate` 回归 `Qt::KeepAspectRatioByExpanding`（等比拉伸拼图无缝填满），完美保障了自适应拼图没有白边和灰色空隙。
  - **在列表模式下**：完全由独立的 `TreeItemDelegate` 掌控，其根本不去获取 `gridMode` 属性，天然隔绝了此属性带来的任何干扰。

---

### 支柱二：非图区预留高度（`extraHeight`）的严格对齐
在之前的重构中，重命名双击失效和卡片拉伸，核心原因在于：自适应布局、缩略图绘制内部各自维护了一套卡片高度常数，导致了数据失准。
* **方案内容**：
  明确规定这三种模式下几何分配与事件判定的“常数唯一性”公式：
  - **自适应视图（JustifiedMode）**与**网格视图（GridMode）**共享同一套卡片边缘计算常数：
    - 统一将卡片的非图预留总高度 `extraHeight` 定义为：
      $$\text{extraHeight} = \text{cardPadding} (6px) + \text{textHeight} (36px) + \text{ratingHeight} (20px) + \text{gap} (4px) = 64px$$
  - 在 `JustifiedView::mouseDoubleClickEvent` 双击事件定位中，对“双击图片”还是“双击文字重命名”的划分界限，必须与这个常数进行 **100% 物理对齐**：
    ```cpp
    // 双击判定：高度划分解耦
    QRect textRect(itemRect.left(), itemRect.bottom() - textHeight, itemRect.width(), textHeight);
    QRect thumbRect(itemRect.left(), itemRect.top(), itemRect.width(), itemRect.height() - extraHeight);
    ```
* **如何防范干扰**：
  不管在 `GridMode` 下对缩放做了何种修改，双击判定区域都与布局排版的可用图片高度严格同步，杜绝了“改了网格大小，自适应双击失效”的耦合。

---

### 支柱三：列表模式（ListView）与缩放比例（`m_zoomLevel`）的安全阀
* **方案内容**：
  在 `ContentPanel::updateGridSize()` 缩放调整函数中，为三种模式划定各自的安全边界值，拦截任何溢出或干扰行为：
  - **列表模式最高拦截线**：限制在 `m_zoomLevel <= 96` 范围内。一旦滚轮放大导致超过 `96px`，强行自动无缝切入到 `GridView`（网格卡片），防止列表高亮被撑大变形。
  - **网格模式最低拦截线**：网格缩放物理最小值严格锁定为 `96px`（`qBound(96, m_zoomLevel, 128)`）。一旦缩小低于 `96px`，自动无缝降级滑入到 `ListView`（列表行高 80px），防范卡片被缩得畸形。
  - **行高解耦**：列表视图通过 `TreeItemDelegate` 的直角全贯穿式高亮进行渲染，不设置卡片圆角 and 阴影，保留最纯粹、高效的纯文本排布。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 共有三种模式，自适应、网格、列表 | 在方案中，为三种模式在布局、状态和绘制上建立长效隔离机制 | ✅ 一致 |
| 2    | 为什么总是顾此失彼？修复好了这个却破坏了另一个 | 梳理和规范了常数耦合、属性丢失、缩放冲突的拦截手段 | ✅ 一致 |
| 3    | 各自按照各自的模式去显示 | 隔离后，自适应拼图保持无白边铺满，网格保持正方形不拉伸，列表行高自适应 | ✅ 一致 |

## 4. 详细解决方案说明

在未来的开发中，任何对三大模式的改动都必须严格遵守本方案所界定的代码边界：

```cpp
// 1. JustifiedView.cpp (状态设置)
void JustifiedView::setLayoutMode(LayoutMode mode) {
    if (m_layoutMode != mode) {
        m_layoutMode = mode;
        // 动态设置契约属性。这是三种模式互不干扰的最高核心逻辑
        setProperty("gridMode", m_layoutMode == GridMode);
        scheduleLayout();
    }
}

// 2. ThumbnailDelegate.cpp (卡片绘制：自适应与网格分流)
bool isGrid = option.widget ? option.widget->property("gridMode").toBool() : false;
QPixmap scaled = thumb.scaled(m.cardRect.size(),
                              isGrid ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding,
                              Qt::SmoothTransformation);

// 3. TreeItemDelegate.h (列表行绘制：100% 独立于卡片渲染)
void TreeItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    // 直角全贯穿式纯色高亮渲染，不读任何 gridMode，绝对不干扰卡片
    painter->drawRect(option.rect);
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/JustifiedView.cpp` (属性同步及双击判定对齐)、`src/ui/ContentPanel.cpp` (缩放对齐)

**明确禁止越界修改的范围：**
- [ ] 严禁修改列表视图内部的数据模型、标签管理以及底层 USN 磁盘扫描机制。

## 6. 实现准则与预警【核心】
1. **防止常数冲突**：对 `textHeight` (36px)、`ratingHeight` (20px)、`gap` (4px) 三个常数，在双击判定和 doLayout 计算时必须由同一组逻辑分配，禁止任何单边常数漂移。
2. **QObject 属性感知机制**：当进行多视图模式动态切换时，动态属性 `setProperty` 具有毫秒级的元对象响应，可以确保界面在切换完成的瞬间，Delegate 能够立刻刷新缩放策略，完全做到了无感刷新。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 纯分析师模式 | 未收到明确“批准执行”前，禁止修改任何代码文件 | ✅ 符合，当前仅创建方案文件，绝对不修改任何代码 |

## 8. 待确认事项（可选）
- 无。
