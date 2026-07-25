# 侧边栏分类计数偶发归零问题深度排查报告

经过对侧边栏树形模型 `CategoryModel`、主面板容器 `CategoryPanel`、数据访问层 `CategoryRepo` 以及元数据管理中心 `MetadataManager` 源码的深入剖析与时序建模，我们锁定了导致侧边栏分类计数偶尔正确、偶尔显示为 `0` 这一严重Bug的根本原因。

本报告绝不发散、不扩展任何未提及的需求，仅客观直击最核心、最底层的时序竞争与逻辑设计缺陷。

---

## 核心根源一：空值保护防御机制物理失效

在 `CategoryPanel.cpp` 的刷新定时器超时响应槽函数中，存在如下一段“物理修复”过滤逻辑，目的是在统计数据为空时拒绝刷新界面防止计数归零：

```cpp
// 2026-07-xx 物理修复：若统计数据全空，且系统尚未加载完成，则拒绝执行 UI 更新以防止计数清零
if (sysCounts.isEmpty() && catCounts.isEmpty()) {
    return;
}
```

### 致命设计缺陷：
- **`sysCounts` 永远不可能为空 (`isEmpty() == false`)**：  
  `CategoryRepo::getSystemCounts()` 返回一个包含系统预设项的 `QMap<QString, int>`：
  ```cpp
  QMap<QString, int> CategoryRepo::getSystemCounts() {
      QMap<QString, int> res;
      res["all"] = s_totalCount.load();
      res["tags"] = s_tagsCount.load();
      res["recently_visited"] = s_recentlyVisitedCount.load();
      res["untagged"] = s_untaggedCount.load();
      res["uncategorized"] = s_uncategorizedCount.load();
      res["trash"] = s_trashCount.load();
      return res;
  }
  ```
  即使当中的原子计数值（如 `s_totalCount` 等）**全部为 `0`**，`getSystemCounts()` 返回的 Map 中也依然会牢牢保留 `all`, `tags`, `recently_visited` 等 6 个键值对（Key）。
  由于 Map 包含 6 个 Key，`sysCounts.isEmpty()` 得到的结果必然是 `false`。
- **后果**：  
  这导致所谓的保护性校验 `sysCounts.isEmpty() && catCounts.isEmpty()` 永远无法成立。当系统计算出所有项为 `0` 的错误结果时，这段防护代码无法成功拦截，依然允许将 `0` 覆盖写入 UI 树。

---

## 核心根源二：异步启动时序竞争与数据库污染（Dirty Write）

系统启动链是由 `CoreController::startSystem()` 引导的。为了防止阻塞界面，元数据加载在异步工作线程中执行：

```cpp
// 线程 A：CoreController -> MetadataManager::initFromScchMode() 异步初始化缓存
```

### 致命时序竞争链条：
1. **零计数初值载入**：在 `MetadataManager::initFromScchMode()` 的末尾，由于此时本地的 `m_cache` 刚刚载入，系统立即调用 `CategoryRepo::loadStatsFromDb()` 从 SQLite 数据库的 `system_stats` 表中载入上一次保存的历史计数存根（如 `sys_total_count`），载入后通过定时器 `m_uiSignalTimer`（200ms）在事件循环中派发全量重建信号 `__RELOAD_ALL__`。
2. **两股刷新脉冲的并发碰撞**：
   - **第一股脉冲**：`CoreController` 的异步启动线程结束，回到主线程发出 `CoreController::initializationFinished` 信号。`CategoryPanel` 响应此信号，强行将 `m_isFirstLoad` 设为 `true` 并调用 `requestRefresh()`。
   - **第二股脉冲**：`MetadataManager` 的 `m_uiSignalTimer` 超时触发，发出 `metaChanged("__RELOAD_ALL__")` 信号。`CategoryPanel` 响应此信号，调用 `requestRefresh(true)`，从而在 `m_refreshTimer` 中设置 `needsFullRebuild = true`。
3. **元数据尚未就绪即开始强制盘点**：
   - 当 `CategoryPanel` 超时刷新时，它会通过 `QtConcurrent::run` 在另一个后台工作线程中并发调度 **`CategoryRepo::fullRecount()`** 进行全量重新对账：
     ```cpp
     auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
     ```
   - **极端时序冲突**：如果此时 `MetadataManager` 仍在执行由于多库挂载引发的二次扫描、或处于特定的缓存重构/清理临界区间，使得内存中的 `m_cache` 还是空的、或已被临时清空（`m_cache.size() == 0`）。
   - 这时 `getLightweightCacheSnapshot()` 将返回一个**全空**的文件快照列表！
4. **致命的写回覆盖（Dirty Write）**：
   - `fullRecount()` 拿着这个全空的快照列表进行累加，理所当然地算出了 `total = 0`, `uncategorized = 0`, `trash = 0`。
   - 随后，它干了两件毁灭性的事：
     1. 将内存中各原子计数器（`s_totalCount` 等）全部**重置覆盖为 `0`**。
     2. 发起数据库事务，执行 `INSERT OR REPLACE INTO system_stats ...` 将 `sys_total_count = 0` 等**持久化写回数据库**。
5. **导致下一次启动也是 `0`**：
   - 一旦脏数据覆盖了数据库，下一次程序启动时，`loadStatsFromDb()` 载入的历史存根自然也就是 `0`，直到后续某个偶然机会触发了正确的 `recount` 才会恢复。

---

## 核心根源三：树结构全量重建的硬编码置零副作用

在 `CategoryModel.cpp` 中：
```cpp
void CategoryModel::refresh() {
    beginResetModel();
    // ... 构建整个分类树 ...
    // 所有系统分类、自定义分类在初始化时，均会被硬编码格式化为 (0) 计数
    item->setText(QString("%1 (0)").arg(name));
    endResetModel();
}
```

### 逻辑逻辑缺陷：
- 每次触发 `CategoryPanel` 的重建流程（比如收到 `initializationFinished` 或 `__RELOAD_ALL__`）时，只要 `needsFullRebuild` 为真，都会首先执行 `m_categoryModel->refresh()`。
- 这会导致界面上的分类标题瞬间**全部被重置为 `(0)`**。
- 这是一种“先破后立”的设计：它依赖后续异步并发跑完 `CategoryRepo::fullRecount()` 并通过消息队列执行 `updateStatistics` 将真实计数回填。
- **后果**：
  如果计算线程发生了**线程饥饿、死锁、数据库读写锁冲突（SQLite `database is locked` 导致对账失败直接退出）**，回填局部的 `updateStatistics` 将永远不会被调用。
  此时，侧边栏分类的计数就只能悲惨地卡死在刚被 `refresh()` 重置后的 `(0)` 状态。

---

## 总结

综上所述，侧边栏计数偶发归零，不是由于单一因素导致，而是**“UI刷新默认清零 -> 异步对账时序竞争 -> 空值快照生成 -> 脏数据覆盖写回数据库 -> 过滤防御机制失效”**这五重致命时序缺陷交织造成的系统性恶性Bug。
