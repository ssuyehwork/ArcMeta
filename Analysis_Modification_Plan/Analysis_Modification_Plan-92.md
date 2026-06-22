# 筛选器架构统一与日期排序交互增强方案 —— Analysis_Modification_Plan-92.md

## 1. 任务背景
本方案旨在消除 ArcMeta 搜索与筛选之间的逻辑双轨制，解决导航重置漏洞，并通过移除高负载标签区块及引入日期排序功能，提升筛选面板的性能与易用性。

## 2. 问题定位
- **状态不透明**：搜索关键词与筛选状态在 `FilterProxyModel` 中双轨运行，导致状态同步复杂且重置不彻底。
- **渲染压力**：侧边栏标签区块在大规模数据下产生剧烈的 UI 渲染开销，且功能与搜索框重叠。
- **交互瓶颈**：日期筛选列表固定排序，缺乏逆序排列手段，导致用户难以快速定位最新数据。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将 keyword 合并进 FilterState 并移除标签字段 | 方案 4.1：数据结构重定义 | ✅ |
| 2    | 彻底移除“标签 / 关键字”区块 | 方案 4.2.1：UI 与逻辑清理 | ✅ |
| 3    | 日期区块增加升降序按钮并保留勾选状态 | 方案 4.2.2：日期重排算法 | ✅ |
| 4    | 信号链统一化，移除 blockSignals 手动补救 | 方案 4.3：MainWindow 连接重整 | ✅ |

## 4. 详细解决方案

### 4.1 数据结构与模型层重整 (FilterState & FilterProxyModel)
1. **FilterState (src/ui/FilterPanel.h)**:
   - 新增 `QString keyword;` 字段，默认值为空。
   - 彻底移除 `QStringList tags;` 及 `QString tagFilterText;` 字段。
2. **FilterProxyModel (src/ui/ContentPanel.cpp)**:
   - 移除成员 `QString m_searchQuery;` 及其相关 Setter。
   - 重构 `filterAcceptsRow`：
     - 删除原“3. 标签过滤”判断块。
     - 将末尾文件名匹配逻辑改为读取 `currentFilter.keyword`。

### 4.2 FilterPanel UI 与功能调整
1. **标签区块清理**：从 `rebuildGroups()` 中彻底删除“标签 / 关键字”整个构建代码块，并在 `clearAllFilters()` 中移除对应输入框的清理代码。
2. **日期排序功能实现**：
   - **状态维护**：在 `FilterPanel` 中新增 `m_createDateDesc = true` 和 `m_modifyDateDesc = true` 成员。
   - **UI 注入**：利用 `buildGroup` 暴露的 `hdrLayout`，在“创建日期”和“修改日期”标题栏右侧追加 16x16px 透明按钮（使用 `arrow_up`/`arrow_down` 图标）。
   - **排序逻辑**：新增 `rebuildDateCheckboxes(DateType, bool descending)`。
     - 步骤：1. 暂存 `m_filter.createDates` 等勾选状态；2. 对日期 Keys 进行排序；3. 清空并重建该区块复选框；4. 物理恢复勾选状态并发射单次信号。

### 4.3 MainWindow 信号链统一化 (src/ui/MainWindow.cpp)
1. **简化 doSearch**：移除所有 `blockSignals` 及手动调用 `applyFilters` 的代码。
2. **状态合并路由**：在 `filterChanged` 的 Lambda 槽函数中，实时获取搜索框文字并合并至 `FilterState::keyword`，再统一下发。
3. **导航重置**：确保 `unifiedNavigateTo` 调用的 `clearAllFilters` 能够物理清除代理模型内的关键词状态。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/FilterPanel.cpp / .h`: 标签移除、日期排序按钮。
- [ ] `src/ui/ContentPanel.cpp / .h`: `FilterProxyModel` 逻辑重整。
- [ ] `src/ui/MainWindow.cpp`: 信号链重构。

**明确禁止越界修改的范围：**
- [ ] 禁止修改任何 UI 控件复用（UI Pool）逻辑（留待后续任务）。
- [ ] 禁止修改 `MetadataManager` 底层算法或数据库结构。

## 6. 实现准则与预警【核心】
1. **信号隔离预警**：在日期重排重建复选框期间，必须调用 `blockSignals(true)`，防止中间态触发频繁过滤。
2. **空值兼容预警**：确保 `applyFilters` 在 keyword 为空时依然能正确回退到全量显示状态。
3. **图标资源预警**：按钮图标颜色需统一使用 `Style::TextMuted` 规格，保持侧边栏视觉一致性。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 搜索职责 | 搜索必须通过 CoreController 覆盖标签 | ✅ 审计确认搜索框已具备此能力 |
| UI 实现标准 | QLineEdit 使用 setClearButtonEnabled | ✅ 移除标签区块过程中需核对剩余输入框 |
| 信号防抖 | 合理利用 m_uiSignalTimer | ✅ 统一化路径有助于防抖窗口生效 |

## 8. 待确认事项
- [ ] 移除标签区块后，`MainWindow` 中 `directoryStatsReady` 传递的 `tagCounts` 建议改为在 `populate` 中忽略以维持接口稳定。
