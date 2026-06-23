# “目录导航”面板自适应高度与 DND 兼容架构演进 —— Analysis_Modification_Plan-96.md

## 1. 任务背景
用户反馈“目录导航”面板布局死板且产生大面积留白。在尝试优化布局高度自适应的过程中，必须确保原有的“拖拽收藏”功能不被破坏，尤其是当面板条目较少时，下方空白区域仍需能接收拖拽投放。

## 2. 问题定位
- **布局局限**：`QSplitter` 导致固定比例留白。
- **DND 逻辑风险**：若磁盘树与收藏夹均使用 `setFixedHeight` 随内容完全收缩，当条目较少时，面板下方将出现“非交互式”布局空白区，导致用户拖拽到该区域时因无有效 Widget 接收而失效。
- **层级屏蔽**：引入 `QScrollArea` 可能在事件流传递上对底层 `DropTreeView` 产生干扰。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 随着树状伸展而向下/向上伸展缩进 | 磁盘树执行 `setFixedHeight` 随内容动态收缩 | ✅ |
| 2    | 不被支持拖拽收藏了 | 收藏夹区域执行 `Minimum-Expanding` 策略，填充所有余白 | ✅ |
| 3    | 不曾要求多添加一个标题 | 移除“本地磁盘”多余标题 | ✅ |

## 4. 详细解决方案

### 4.1 混合弹性布局架构 (Hybrid Elastic Layout)
为了兼顾“高度收缩”与“拖拽靶场”，采用差异化高度策略：
1. **磁盘树 (m_treeView)**：
   - **行为**：**完全收缩**。
   - **实现**：监听信号，通过 `setFixedHeight` 使其高度精确等于可见行数 * 28px。
   - **目的**：消除磁盘列表下方的内部留白，让“收藏夹标题”紧随其后。
2. **收藏夹列表 (m_favoriteView)**：
   - **行为**：**向底填充**。
   - **实现**：**严禁设置 `setFixedHeight`**。将其 `sizePolicy` 设为 `(Expanding, Expanding)`。
   - **目的**：确保即使收藏夹内没有条目，该视图也会在布局中占据从收藏夹标题到面板底部的**所有剩余物理空间**，作为一个巨大的有效“放置靶场”。

### 4.2 DND 物理增强与连接保障
1. **类实例化强制要求**：
   - 必须使用 `DropTreeView` 子类实例化 `m_treeView` 和 `m_favoriteView`。
2. **属性与信号校准**：
   - 显式调用 `setAcceptDrops(true)`。
   - 必须在 `initUi` 中重新建立连接：`connect(m_favoriteView, &DropTreeView::pathsDropped, this, &NavPanel::onPathsDroppedToFavorite);`。
3. **事件穿透处理**：
   - 为防止 `QScrollArea` 拦截拖拽，需对其 viewport 设置 `m_scrollArea->viewport()->setAcceptDrops(false)`（使事件透传至其下的布局子项）或确保子项布局覆盖全域。

### 4.3 物理对齐规范
1. **标题栏**：仅保留“目录导航”主标题和“★ 收藏夹”子标题。
2. **边距**：
   - 移除 `QSplitter`。
   - 磁盘树与收藏夹标题紧密堆叠，组间距 0px。
   - 磁盘树左侧通过样式表 `padding-left: 15px` 维持呼吸感。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/NavPanel.h/cpp`：布局重构与高度动态调整逻辑。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `DropTreeView.cpp` 中的底层 DND 处理算法。
- [ ] 禁止修改磁盘读取的异步并发逻辑。

## 6. 实现准则与预警【核心】

1. **高度计算触发点**：
   - 磁盘树：`expanded`, `collapsed`, `rowsInserted`, `rowsRemoved`, `modelReset`。
2. **防抖处理**：使用 `QTimer::singleShot(0, this, [this]{ updateDiskTreeHeight(); });`。
3. **关键预警**：在 `updateDiskTreeHeight` 中，计算公式应为 `indexAt(rect().bottomLeft()).isValid() ? ... : ...` 或递归统计可见项。若计算不准，将导致收藏夹标题位置偏移。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| DND 响应 | 面板空闲区域应能接收拖拽 | ✅ (收藏夹 Extending 填充) |
| 标题高度 | 统一 32px | ✅ (对齐) |
| 样式表 | QTreeView 需设置 outline:none | ✅ (对齐) |

## 8. 待确认事项（可选）
- 无。
