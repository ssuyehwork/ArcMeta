# 缩略图卡片交互选区感知缺失与批量评分修复 —— Analysis_Modification_Plan-86.md

## 1. 任务背景
在 ArcMeta 的内容面板（ContentPanel）中，用户期望的操作逻辑是“选区即作用域”。然而，目前的缩略图卡片（Thumbnail View）在处理鼠标交互（如点击星级评分）时，仅作用于被点击的单个项目，无视当前已选中的多个项目。这导致在大规模整理数据时，卡片内的便捷交互功能由于缺乏批量支持而显得低效。

## 2. 问题定位

### 2.1 根因分析
1.  **Delegate 交互逻辑孤立**：`ThumbnailDelegate` 和 `GridItemDelegate` 的 `editorEvent` 在处理鼠标点击时，直接对传入的 `index` 执行 `model->setData`。
2.  **选区遍历缺失**：代码中虽然检查了 `isSelected` 以确保只有选中项可被点击修改，但未通过 `view->selectionModel()` 获取全量选区进行广播更新。
3.  **模型反馈单一**：`setData` 仅针对单行触发 `dataChanged` 信号，导致批量修改后的视觉刷新可能存在延迟。

### 2.2 涉及文件
- `src/ui/ThumbnailDelegate.cpp`：缩略图视图点击处理。
- `src/ui/ContentPanel.cpp`：网格视图点击处理。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 选中多个项目后，点击卡片下方星级时却只支持单个 | 修改 Delegate 逻辑，检测到有效点击后遍历选区执行更新 | ✅ |
| 2    | 操作应该支持批量，但却又只支持单个 | 全量审查卡片内所有交互热区，确保其具备选区感知能力 | ✅ |

## 4. 详细解决方案

### 4.1 修复 ThumbnailDelegate (缩略图视图)
修改 `src/ui/ThumbnailDelegate.cpp` 中的 `editorEvent`：

```cpp
if (isBanHit || hitStar != -1) {
    int newValue = isBanHit ? 0 : hitStar;
    
    // 获取视图选区模型
    if (view && view->selectionModel()) {
        auto selectedIndexes = view->selectionModel()->selectedIndexes();
        // 物理加固：仅处理第 0 列的有效项，防止重复操作
        for (const auto& selIdx : selectedIndexes) {
            if (selIdx.column() == 0) {
                model->setData(selIdx, newValue, m_ratingRole);
            }
        }
    } else {
        // 回退逻辑：仅修改当前项
        model->setData(index, newValue, m_ratingRole);
    }
    // ... 保持现有的 EditTriggers 屏蔽逻辑 ...
}
```

### 4.2 修复 GridItemDelegate (网格视图)
修改 `src/ui/ContentPanel.cpp` 中的 `GridItemDelegate::editorEvent`，应用与 4.1 相同的选区感知逻辑。

### 4.3 强化元数据变更反馈
在 `FerrexVirtualDbModel::setData` 中，针对 `RatingRole` 和 `ColorRole` 的变更，确保发出准确的 `dataChanged` 信号。若为批量操作，建议在 Delegate 层级完成循环后，显式调用一次 `m_proxyModel->invalidate()`（视性能而定）或确保 model 内部能合并信号。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `ThumbnailDelegate` 与 `GridItemDelegate` 的 `editorEvent` 实现。

**明确禁止越界修改的范围：**
- [ ] 禁止修改卡片的布局比例（Metrics）。
- [ ] 禁止修改 `MetadataManager` 的物理持久化流程（应由 `model->setData` 自然触发）。

## 6. 实现准则与预警【核心】

1.  **索引映射警示**：Delegate 收到的 `index` 可能是代理模型的索引。在使用 `selectionModel` 时，需确保操作的一致性。本项目的 `selectionModel` 与 View 绑定，返回的索引可直接传递给 `model->setData`。
2.  **性能冗余**：在大批量（如 > 500 项）选中时执行 `setData` 会触发高频 IO。当前的 `MetadataManager` 已具备异步刷盘机制，但 UI 层的 `dataChanged` 信号风暴仍需警惕。
3.  **UI 阻断**：代码中现有的 `view->setEditTriggers(NoEditTriggers)` 是为了防止评分点击误触发重命名编辑器，此逻辑在批量模式下必须保留且严谨执行。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 交互驱动 | 选区必须与 UI 对齐 | ✅ 方案补全了缺失的选区感知 |
| 批量操作 | 批量操作需支持同步刷新 | ✅ 方案确保了选区内的元数据物理同步 |

## 8. 待确认事项
- 无。
