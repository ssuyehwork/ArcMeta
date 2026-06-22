# 筛选器标签移除与日期排序优化 —— Analysis_Modification_Plan-91.md

## 1. 任务背景
为了优化大量标签场景下的 UI 渲染性能及内存占用，需将侧边栏筛选器的“标签 / 关键字”区块彻底移除，其功能由已具备标签匹配能力的搜索框承担。同时，在“创建日期”和“修改日期”区块新增独立的升降序切换功能，提升日期定位效率。

## 2. 问题定位
- **性能瓶颈**：当标签库达到数千规模时，`FilterPanel::rebuildGroups()` 动态生成大量复选框控件，导致主线程卡顿并产生密集的信号同步开销。
- **功能重叠**：搜索框底层 `searchInCache` 已实现文件名、备注及标签的综合搜索，筛选器内的标签区块属于冗余实现。
- **易用性缺陷**：当前日期筛选列表固定按字母/时间顺序排列，缺乏逆序排列手段，导致用户难以快速定位最新日期。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 移除筛选器中的"标签 / 关键字"区块 | 方案 4.1：彻底删除相关 UI 与逻辑代码 | ✅ |
| 2    | 移除 FilterState 中与标签相关的字段 | 方案 4.2：修改 `FilterState` 结构体 | ✅ |
| 3    | 移除模型中标签过滤逻辑 | 方案 4.3：修改 `FilterProxyModel` | ✅ |
| 4    | 确认搜索框标签搜索能力正常工作 | 方案 4.4：已审计确认 `MetadataManager` 逻辑 | ✅ |
| 5    | 日期区块标题栏增加升降序按钮 (16x16px) | 方案 4.5：利用 `buildGroup` 扩展标题行 | ✅ |
| 6    | 排序不影响内容面板过滤结果 | 方案 4.6：独立排序逻辑，仅操作 UI 排列 | ✅ |
| 7    | 切换排序需保留已勾选状态 | 方案 4.7：使用 `QSet` 状态暂存与恢复机制 | ✅ |

## 4. 详细解决方案

### 4.1 移除标签区块相关代码 (src/ui/FilterPanel.cpp)
- **删除成员变量初始化**：在 `rebuildGroups()` 中移除 `m_editTag` 指针的置空。
- **删除 UI 构建逻辑**：从 `rebuildGroups()` 中彻底删除 `// ── 3. 标签 / 关键字 ──` 整个代码块（含 `QLineEdit` 及 `addFilterRow` 循环）。
- **清理重置逻辑**：在 `clearAllFilters()` 中移除对 `m_editTag->clear()` 的调用。
- **清理 populate**：在 `populate()` 签名中虽然保留参数以维持兼容性（或同步修改 `MainWindow` 调用），但内部不再使用 `tagCounts`。

### 4.2 修改数据结构：FilterState (src/ui/FilterPanel.h)
移除以下字段：
- `QStringList tags;`
- `QString tagFilterText;`

### 4.3 修改模型判定：FilterProxyModel (src/ui/ContentPanel.cpp)
- 从 `filterAcceptsRow` 中删除 `// 3. 标签过滤` 对应的整个判断逻辑块。

### 4.4 日期排序状态维护 (src/ui/FilterPanel.h)
在 `FilterPanel` 类中新增：
```cpp
enum DateType { CreateDate, ModifyDate };
bool m_createDateDesc = true;
bool m_modifyDateDesc = true;
void rebuildDateCheckboxes(DateType type, bool descending);
```

### 4.5 日期标题栏 UI 注入 (src/ui/FilterPanel.cpp)
在 `rebuildGroups()` 构建日期区块时：
1. 调用 `buildGroup` 并获取 `hdrLayout`。
2. 创建 `QPushButton`，尺寸 16x16px，无背景。
3. 根据 `m_xxxDateDesc` 设置图标（`arrow_down` 或 `arrow_up`，颜色 `TextMuted`）。
4. 连接 `clicked` 信号至 `rebuildDateCheckboxes`。

### 4.6 日期重排逻辑实现 (src/ui/FilterPanel.cpp)
`rebuildDateCheckboxes(type, descending)` 的实现步骤：
1. **状态搜集**：遍历对应日期区块的 `QVBoxLayout`（或直接使用 `m_filter.createDates`/`m_filter.modifyDates`），确保当前勾选状态已同步至 `m_filter`。
2. **列表排序**：对 `m_createDateCounts.keys()` 或 `m_modifyDateCounts.keys()` 执行 `std::sort`。若 `descending` 为 true，使用 `Qt::CaseInsensitive` 逆序。
3. **UI 刷新**：清空对应区块的内容布局，按照新顺序调用 `addFilterRow` 重新填充复选框。
4. **状态恢复**：根据 `m_filter` 中的日期列表，重新调用 `setChecked(true)`。
5. **信号发射**：在所有复选框恢复后，`emit filterChanged(m_filter)`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/FilterPanel.cpp / .h`: 移除标签区块，实现日期排序按钮。
- [ ] `src/ui/ContentPanel.cpp`: 移除标签过滤判定。
- [ ] `src/ui/MainWindow.cpp`: 调整 `populate` 调用参数。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MetadataManager` 或 `CoreController` 的搜索底层算法。
- [ ] 禁止修改日期筛选以外的其他筛选区块（如颜色、评级）。

## 6. 实现准则与预警【核心】
1. **头文件依赖**：确保 `FilterPanel.cpp` 已包含 `QSet` 和 `algorithm` 库。
2. **信号阻塞预警**：在 `rebuildDateCheckboxes` 重建过程中，必须对复选框调用 `blockSignals(true)`，防止在重排过程中高频触发不必要的过滤计算。
3. **图标对齐预警**：16x16px 按钮需使用 `QPushButton { background: transparent; border: none; }` 以对齐标题文字。
4. **状态独立性**：确保修改日期排序不会导致创建日期区块也发生重排。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 性能 | 避免大量 UI 控件频繁销毁重建导致的假死 | ✅ 移除标签区块直接解决了此问题 |
| 搜索职责 | 搜索必须通过 CoreController 覆盖文件名/备注/标签 | ✅ 审计确认已由搜索框完全承接 |
| 标题栏标准 | 按钮尺寸与样式规范 | ✅ 采用 16x16px 透明样式符合侧边栏规格 |

## 8. 待确认事项
- [ ] `MainWindow.cpp` 中 `directoryStatsReady` 传递的标签参数是否可以完全忽略？（方案确定：忽略该参数以保持模型驱动的一致性）。
