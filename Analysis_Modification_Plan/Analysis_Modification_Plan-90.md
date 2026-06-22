# 过滤逻辑统一化重构方案 —— Analysis_Modification_Plan-90.md

## 1. 任务背景
当前 ArcMeta 的搜索框关键词（`keyword`）与右侧筛选器（`FilterState`）在 `FilterProxyModel` 中采用双轨运行。搜索词通过 `setSearchQuery` 独立进入模型，而筛选器通过 `applyFilters` 进入。这导致了状态同步复杂、接线逻辑脆弱以及导航切换时重置不彻底等问题。

## 2. 问题定位
- **状态不透明**：`FilterState` 缺乏对 `keyword` 的感知，导致两者无法通过单一信号路由。
- **旁路逻辑脆弱**：在 `MainWindow::doSearch` 中，为了防止旧筛选拦截搜索结果，不得不使用 `blockSignals` + 手动 `applyFilters` 的补救措施。
- **重置漏洞**：`unifiedNavigateTo` 触发的 `clearAllFilters` 无法物理清除 `proxy model` 内残留的 `m_searchQuery` 字符串。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将 keyword 合并进 FilterState | 方案 4.1：修改 `src/ui/FilterPanel.h` | ✅ |
| 2    | 统一通过 applyFilters 路径进入 proxy model | 方案 4.2：重构 `FilterProxyModel` 逻辑 | ✅ |
| 3    | 移除 blockSignals + 手动同步补救逻辑 | 方案 4.3：简化 `MainWindow` 连接 | ✅ |
| 4    | clearAllFilters 必须触发 filterChanged | 方案 4.4：核对 `FilterPanel.cpp` | ✅ |

## 4. 详细解决方案

### 4.1 修改数据结构：FilterState (src/ui/FilterPanel.h)
在 `FilterState` 结构体末尾新增关键词字段。
```cpp
struct FilterState {
    // ... 现有字段 ...
    QString keyword; // 默认值为空字符串
};
```

### 4.2 重构模型层：FilterProxyModel (src/ui/ContentPanel.cpp / .h)
1. **成员移除**：从 `FilterProxyModel` 中移除 `QString m_searchQuery;`。
2. **逻辑合并**：在 `filterAcceptsRow` 的末尾，将对文件名包含关键词的判定改为读取 `currentFilter.keyword`。
3. **接口退役**：废弃 `setSearchQuery(const QString& query)`，其功能由 `updateFilter()` 统一触发。
```cpp
// filterAcceptsRow 尾部逻辑优化
if (currentFilter.keyword.isEmpty()) return true;
QString fileName = idx.data(Qt::DisplayRole).toString();
return fileName.contains(currentFilter.keyword, Qt::CaseInsensitive);
```

### 4.3 MainWindow 接线重整 (src/ui/MainWindow.cpp)
1. **filterChanged 槽函数增强**：
   在 `MainWindow::initUi` 的连接处，动态合并搜索框文本。
   ```cpp
   connect(m_filterPanel, &FilterPanel::filterChanged, this, [this](const FilterState& state) {
       FilterState merged = state;
       merged.keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
       m_contentPanel->applyFilters(merged);
       updateStatusBar();
   });
   ```
2. **doSearch 逻辑简化**：
   移除所有 `blockSignals` 和手动 `applyFilters` 调用。
   ```cpp
   auto doSearch = [this](const QString& keyword) {
       // ... 之前的 mode 校验 ...
       if (m_filterPanel) {
           m_filterPanel->clearAllFilters(); // 触发信号，槽函数自动完成 keyword 合并
       }
       // ... 维护历史记录与 performSearch ...
   };
   ```

### 4.4 确保重置信号：FilterPanel (src/ui/FilterPanel.cpp)
确认 `clearAllFilters()` 内部已存在 `emit filterChanged(m_filter);` 且未被屏蔽。根据审计，该逻辑已存在，符合重构前提。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/FilterPanel.h`: `FilterState` 结构体。
- [ ] `src/ui/FilterPanel.cpp`: `clearAllFilters` 逻辑核对。
- [ ] `src/ui/ContentPanel.cpp / .h`: `FilterProxyModel` 成员与过滤判定。
- [ ] `src/ui/MainWindow.cpp`: `doSearch` 及 `filterChanged` 信号连接点。

**明确禁止越界修改的范围：**
- [ ] 禁止修改颜色感知计算、评级逻辑等具体过滤判定代码。
- [ ] 禁止改动 `MetadataManager` 持久化逻辑。

## 6. 实现准则与预警【核心】
1. **信号风暴预警**：由于搜索词变化可能触发 `clearAllFilters` 并进而触发 `applyFilters`，需确保在流式加载期间信号路由正确。
2. **文本重置预警**：在 `unifiedNavigateTo` 中，搜索框的 `clear()` 必须与筛选器的重置同步。
3. **空值判定预警**：`applyFilters(FilterState())` 必须确保能让 `FilterProxyModel` 回归无过滤状态（显示全部可见项）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 筛选面板持久化 | AppConfig 记录 AllGroupsCollapsed | ✅ 逻辑不受影响 |
| 搜索行为红线 | 必须传递 DataSource 范围参数 | ✅ 逻辑不受影响 |
| UI 实现标准 | QLineEdit 使用 setClearButtonEnabled | ✅ 逻辑不受影响 |

## 8. 待确认事项
- [ ] 在 `doSearch` 调用 `clearAllFilters()` 时，是否需要显式更新 `m_searchEdit` 的显示？（根据方案，`doSearch` 由 `m_searchEdit` 触发，无需反向更新文字，仅需确保逻辑状态同步）。
