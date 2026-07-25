# 侧边栏分类计数偶发归零问题深度排查与修复方案报告

经过对侧边栏树形模型 `CategoryModel`、主面板容器 `CategoryPanel`、数据访问层 `CategoryRepo` 以及元数据管理中心 `MetadataManager` 源码的深入剖析与时序建模，我们锁定了导致侧边栏分类计数偶尔正确、偶尔显示为 `0` 这一严重Bug的根本原因，并设计了相对应的**非侵入式、职责单一、物理加固**的彻底修复方案。

---

## 一、 问题根本原因深度剖析

### 1.1 空值保护防御机制物理失效

在 `CategoryPanel.cpp` 的刷新定时器超时响应槽函数中，存在如下一段“物理修复”过滤逻辑，目的是在统计数据为空时拒绝刷新界面防止计数归零：

```cpp
// 2026-07-xx 物理修复：若统计数据全空，且系统尚未加载完成，则拒绝执行 UI 更新以防止计数清零
if (sysCounts.isEmpty() && catCounts.isEmpty()) {
    return;
}
```

#### 致命设计缺陷：
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

### 1.2 异步启动时序竞争与数据库污染（Dirty Write）

系统启动链是由 `CoreController::startSystem()` 引导的。为了防止阻塞界面，元数据加载在异步工作线程中执行：

```cpp
// 线程 A：CoreController -> MetadataManager::initFromScchMode() 异步初始化缓存
```

#### 致命时序竞争链条：
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

### 1.3 树结构全量重建的硬编码置零副作用

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

#### 逻辑缺陷：
- 每次触发 `CategoryPanel` 的重建流程（比如收到 `initializationFinished` 或 `__RELOAD_ALL__`）时，只要 `needsFullRebuild` 为真，都会首先执行 `m_categoryModel->refresh()`。
- 这会导致界面上的分类标题瞬间**全部被重置为 `(0)`**。
- 这是一种“先破后立”的设计：它依赖后续异步并发跑完 `CategoryRepo::fullRecount()` 并通过消息队列执行 `updateStatistics` 将真实计数回填。
- **后果**：
  如果计算线程发生了**线程饥饿、死锁、数据库读写锁冲突（SQLite `database is locked` 导致对账失败直接退出）**，回填局部的 `updateStatistics` 将永远不会被调用。
  此时，侧边栏分类的计数就只能悲惨地卡死在刚被 `refresh()` 重置后的 `(0)` 状态。

---

## 二、 修复方案设计

针对上述五重交织的时序缺陷，我们本着**不扩充、不发散、精细化物理加固**的原则，设计如下修复方案。此方案不仅能彻底解决侧边栏计数归零问题，还能大幅优化侧边栏重建时的视觉顺畅度（杜绝频闪置零再闪回的现象）。

### 2.1 修复手段一：MetadataManager 提供状态查询接口，修复 UI 保护校验

#### 2.1.1 在 `MetadataManager.h` 增加公有成员函数 `isLoaded`：
```cpp
// 在 class MetadataManager 声明中追加公有成员
public:
    bool isLoaded() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_loaded;
    }
```

#### 2.1.2 修复 `CategoryPanel.cpp` 的空值更新校验：
将原本永远不会成立的 `sysCounts.isEmpty()`，改造为判断**元数据是否尚未加载完，且计数统计全部为零**。如果尚未就绪且全零，则拒绝执行 UI 更新，保障历史载入的非零存根安全：
```cpp
<<<<<<< SEARCH
            // 计算完成后，通过消息队列回传主线程执行局部 UI 更新
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, sysCounts, catCounts]() {
                if (weakThis && weakThis->m_categoryModel) {
                    // 2026-07-xx 物理修复：若统计数据全空，且系统尚未加载完成，则拒绝执行 UI 更新以防止计数清零
                    if (sysCounts.isEmpty() && catCounts.isEmpty()) {
                        return;
                    }
                    // 第三阶段：执行局部数据更新，杜绝 beginResetModel 引发全量布局计算
                    weakThis->m_categoryModel->updateStatistics(sysCounts, catCounts);
                }
            });
=======
            // 计算完成后，通过消息队列回传主线程执行局部 UI 更新
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, sysCounts, catCounts]() {
                if (weakThis && weakThis->m_categoryModel) {
                    // 物理修复：若统计数据全为0，且系统元数据尚未加载完成，则拒绝执行 UI 更新以防止计数清零
                    bool isSysUnready = !MetadataManager::instance().isLoaded();
                    bool allCountsZero = (sysCounts.value("all", 0) == 0 && sysCounts.value("trash", 0) == 0);
                    if (isSysUnready && allCountsZero) {
                        return;
                    }
                    // 第三阶段：执行局部数据更新，杜绝 beginResetModel 引发全量布局计算
                    weakThis->m_categoryModel->updateStatistics(sysCounts, catCounts);
                }
            });
>>>>>>> REPLACE
```

---

### 2.2 修复手段二：在 `fullRecount` 中杜绝空值缓存的脏数据覆写

在 `CategoryRepo::fullRecount()` 的核心对账环节，一旦获取的快照为空，必须在判定缓存未载入完成时**立刻安全中断退出**，绝不允许用空值重置原子计数器，更不允许执行 SQL 脏写把 `0` 写入 `system_stats` 数据库：

```cpp
<<<<<<< SEARCH
    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
=======
    // 物理加固：若元数据管理器尚未加载完成，且快照为空，拒绝重算以防止内存计数器归零并覆盖数据库
    if (!MetadataManager::instance().isLoaded()) {
        qDebug() << "[Recount] MetadataManager has not finished loading. Abort recount to prevent zeroing stats.";
        return;
    }

    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
>>>>>>> REPLACE
```

---

### 2.3 修复手段三：消除树重建时的“硬编码置零”频闪副作用

在 `CategoryModel::refresh()` 中，重建分类树时不应该简单地将文字一律拼接为 `(0)`，而是应当**同步读取当前内存中的最新有效计数值**进行预填充：

#### 2.3.1 改造系统分类节点的构建文字：
```cpp
<<<<<<< SEARCH
    // 2. 全部数据
    {
        QStandardItem* item = new QStandardItem(UiHelper::getIcon("database", PrimaryBlue, 16), "全部数据 (0)");
        item->setData("all", RoleType);
        m_systemNode->appendRow(item);
    }
=======
    // 获取当前内存中的最新缓存计数，进行树构建时的第一级初始化，杜绝 (0) 频闪副作用
    auto sysCounts = CategoryRepo::getSystemCounts();
    auto catCountsVec = CategoryRepo::getCounts();
    QMap<int, int> catCounts;
    for (const auto& entry : catCountsVec) catCounts[entry.first] = entry.second;

    // 2. 全部数据
    {
        int count = sysCounts.value("all", 0);
        QStandardItem* item = new QStandardItem(UiHelper::getIcon("database", PrimaryBlue, 16), QString("全部数据 (%1)").arg(count));
        item->setData("all", RoleType);
        m_systemNode->appendRow(item);
    }
>>>>>>> REPLACE
```
*(注：其余系统节点如“标签检索”、“快速访问”、“未归类”、“未分类”、“回收站”等，均采用相同方式通过 `sysCounts.value(...)` 预填充其在内存中的最新计数值。)*

#### 2.3.2 改造用户分类节点（自定义分类）的构建文字：
```cpp
<<<<<<< SEARCH
        // 2026-xx-xx 按照 Plan-124：统一规范分类项的展示文字格式为："分类名 (计数)"
        item->setText(QString("%1 (0)").arg(name));
=======
        // 2026-xx-xx 按照 Plan-124：统一规范分类项的展示文字格式为："分类名 (计数)"，预填充最新有效缓存计数
        int count = catCounts.value(cat.id, 0);
        item->setText(QString("%1 (%2)").arg(name).arg(count));
>>>>>>> REPLACE
```

通过这一消除硬编码 `(0)` 的手段，侧边栏重构时由于树重置引发的界面闪变 `(0)`、随后又闪变回真实计数的现象将被彻底根治，提供丝滑完美的无缝更新体验。

---

## 三、 总结
本修复方案从：
1. **输入端**（`CategoryModel::refresh` 预填充非零值）、
2. **处理端**（`CategoryRepo::fullRecount` 安全拦截、拒绝脏写）、
3. **输出端**（`CategoryPanel` 槽函数多重拦截、拒绝零值写入 UI）

三管齐下，全方位彻底封死了因异步时序、资源竞争而导致的分类计数偶发归零链路。且方案极度轻量，逻辑单一纯粹，不带有任何多余发散或冗余包袱，完美契合系统稳定性架构指标。
