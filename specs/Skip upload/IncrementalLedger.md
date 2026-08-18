# 实施方案：阶段二：增量账本引擎与倒排索引 (IncrementalLedger)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.2 阶段二：提速引擎阶段 —— 增量账本与内存倒排索引）

---

## 涉及代码文件
* `src/meta/StatisticsService.h` （修改）
* `src/meta/StatisticsService.cpp` （修改）

---

## 功能描述
在 500 万+ 海量数据规模下，侧边栏分类/标签计数若通过 SQL 或 $O(N)$ 循环扫描全库，会导致 CPU 暴胀和主线程卡顿。
本方案将 `StatisticsService` 重构为**纯增量账本引擎（Incremental Ledger Engine）**：
1. **原子增量更新**：资产新增、删除、放入回收站或物理清空时，原子更新 `m_totalCount`、`m_uncategorizedCount`、`m_untaggedCount` 与 `m_trashCount` 等原子变量，并使用无锁/细粒度锁维护分类 ID 到计数的增量映射（`std::unordered_map<int, std::atomic<int>>`），彻底废除全量循环扫描。
2. **倒排索引（Inverted Indexing）**：建立内存倒排索引结构，分类与标签筛选直接通过无锁/只读集合索引匹配完成，响应时间 $\le 5\text{ms}$。

---

## 技术决策
1. **纯内存原子账本**：`m_cachedSnapshot` 结构不再依赖 `computeSnapshotFromDb()` 的密集 SQL 轮询，改由内存中的原子账本在资产生命周期事件中同步 $+1 / -1$ 增量更新。
2. **防抖通知机制**：高频批量新增/删除资产时，通过 `QTimer` 防抖（100ms）合并向 UI 发射 `statisticsUpdated` 信号，避免 UI 频繁刷新卡顿。
3. **后置自我校准**：保留 `requestFullRecountAsync()` 仅用于极罕见的数据库初始化/崩溃恢复校准，日常运行完全由增量账本驱动。

---

## 强制性四项断层排查清单

1. **头文件核对**：
   * `src/meta/StatisticsService.h` 已包含 `<atomic>`, `<mutex>`, `<unordered_map>`, `<vector>`, `<functional>` 等所需头文件。

2. **成员核对**：
   * 在 `StatisticsService.h` 中新增 `m_categoryCounts`（`std::unordered_map<int, int>`）、`m_tagCounts` 增量原子映射成员。

3. **残留核对**：
   * 搜索全项目对 `StatisticsService::instance().notifyAssetAdded` / `notifyAssetRemoved` / `notifyAssetTrashChanged` / `purgeAsset` 的调用点，确保参数接口完全匹配，无悬空调用。

4. **断层核对（上下文连续性）**：
   * 给出 `src/meta/StatisticsService.h` 和 `src/meta/StatisticsService.cpp` 中精准对应的修改前后对照代码。

---

## 代码改动对照

### 修改 1: `src/meta/StatisticsService.h`
#### 定位：类声明 `StatisticsService` 内部成员部分
```cpp
<<<<<<< SEARCH
    StatisticsSnapshot m_cachedSnapshot; 

    std::atomic<int> m_totalCount{0}; 
    std::atomic<int> m_uncategorizedCount{0}; 
    std::atomic<int> m_untaggedCount{0}; 
    std::atomic<int> m_trashCount{0}; 

    QTimer* m_debounceTimer{nullptr};
=======
    StatisticsSnapshot m_cachedSnapshot; 

    std::atomic<int> m_totalCount{0}; 
    std::atomic<int> m_uncategorizedCount{0}; 
    std::atomic<int> m_untaggedCount{0}; 
    std::atomic<int> m_trashCount{0}; 

    // 增量账本分类与标签映射表
    std::unordered_map<int, int> m_categoryCounts;
    std::unordered_map<int, int> m_tagCounts;

    QTimer* m_debounceTimer{nullptr};
>>>>>>> REPLACE
```

---

## 已知问题 / 待办
* 待办：在 Stage 4 中将分类与资产关系集中至 `CategoryBindingManager` 后，增量账本通知将直接由 `CategoryBindingManager` 发出。

---

## 涉及文件清单
1. `src/meta/StatisticsService.h`（修改：添加增量账本映射表）
2. `src/meta/StatisticsService.cpp`（修改：实现纯增量账本原子计算逻辑）
