# 搜索历史排序逻辑修正分析 —— Analysis_Modification_Plan-55.md

## 1. 现状分析 (Problem Analysis)

### 1.1 逻辑缺陷
目前 `SearchHistoryPanel` 展示搜索历史时，视觉顺序与时间顺序完全相反，导致“最近搜索”项排在面板最下方。

- **数据源分析**：在 `MainWindow.cpp` 中，新产生的搜索关键词通过 `m_searchHistory.prepend(keyword)` 插入列表。因此，索引 `0` 始终存储最新的关键词。
- **渲染逻辑错误**：在 `src/ui/SearchHistoryPanel.cpp` 的 `rebuild()` 函数中，历史项的遍历采用了反向循环：`for (int i = m_history.size() - 1; i >= 0; --i)`。
- **视觉后果**：由于 QVBoxLayout 是从上往下排列控件，循环从数组末尾（最旧的数据）开始创建并添加 Widget，导致最旧的关键词占据了顶端位置。

---

## 2. 需求对照表 (Mandatory Comparison Table)

| 编号 | 用户原话 / 理解 | 我的方案对应点 | 是否一致 |
| :--- | :--- | :--- | :--- |
| 1 | 最近使用过的却排在最下方，而很早以前的却排在最上方 | 确认代码中存在反向循环渲染的逻辑 Bug。 | 是 |
| 2 | 希望最近搜索的排在最上方 | 将渲染循环改为正向遍历，确保索引 0（最新词）优先绘制在布局顶端。 | 是 |

---

## 3. 解决方案设计 (Proposed Solution)

### 3.1 渲染循环修正
必须修改 `src/ui/SearchHistoryPanel.cpp` 中的 `rebuild()` 函数，将反向遍历逻辑纠正为正向遍历。

**代码修改建议：**

```cpp
<<<<<<< SEARCH
        // 历史条目（最新的显示在最上方）
        for (int i = m_history.size() - 1; i >= 0; --i) {
            const QString& keyword = m_history[i];
=======
        // 历史条目（最新的显示在最上方）
        // 2026-06-xx 逻辑纠偏：m_history 索引 0 为最新，应正向遍历以确保最新项在布局顶端
        for (int i = 0; i < m_history.size(); ++i) {
            const QString& keyword = m_history[i];
>>>>>>> REPLACE
```

### 3.2 预期行为验证
1. **即时性**：点击搜索后，新关键词进入 `m_searchHistory` 索引 0，再次打开面板时应出现在第一行。
2. **有序性**：旧关键词在 UI 上依次向下移动。
3. **边界控制**：达到 10 条上限后，索引 9 之后的旧数据消失，UI 保持简洁。

---

## 4. 修改文件列表 (Affected Files)

- `src/ui/SearchHistoryPanel.cpp`：修正 `rebuild` 成员函数的循环方向。

---

## 5. 潜在风险 (Potential Risks)
- **风险等级**：极低。
- **分析**：此修改仅影响 UI 呈现层级的迭代方向，不改变底层 `QStringList` 的数据结构或持久化逻辑，无副作用。
