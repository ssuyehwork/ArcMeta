# 侧边栏分类计数归零与系统持续卡顿深度排查报告 (Analysis_Modification_Plan-26)

## 1. 问题现象分析
用户反馈两类核心问题：
1. **侧边栏计数归零**：启动主程序后，侧边栏各分类计数持续显示为 `(0)`，即便内存中有数据。
2. **持续卡顿不恢复**：在长时间离开后切回程序，界面反应极其缓慢，且这种缓慢不会随着时间推移而“自愈”。

## 2. 核心根源定位 (Root Cause)

经过深度对比“旧版本-4”并走读代码逻辑，确认了两个相互叠加的致命缺陷。

### 2.1 致命死锁：读写锁非法升级 (Deadlock in fullRecount)
这是导致“持续卡顿、不恢复”以及“计数卡死在0”的最根本原因。

**逻辑链路：**
1. 在 `MetadataManager::initFromScchMode` 的末尾，调用了 `CategoryRepo::fullRecount()`。
2. `fullRecount()` 内部调用了 `MetadataManager::instance().forEachCachedItem(...)`。
3. **重点**：`forEachCachedItem` 在执行时获取了 `m_mutex` 的 **`shared_lock` (读锁)**。
4. 在回调 lambda 函数中，代码执行了：
   ```cpp
   if (isCategorized || meta.hasUserOperations()) {
       MetadataManager::instance().setManaged(path, true, false);
   }
   ```
5. `setManaged` 内部试图获取 `m_mutex` 的 **`unique_lock` (写锁)**。
6. **死锁发生**：在 `std::shared_mutex` 规范下，**严禁在持有读锁的同一个线程中申请写锁**。这会导致该线程永久挂起（等待自己释放读锁以获取写锁）。

**后果：**
- **后台初始化线程永久挂起**：因此 `initializationFinished` 信号永远不会发射。
- **元数据读写完全锁死**：后续所有涉及元数据修改（如点击文件触发的视觉提取、用户点击标记星级等）的操作都会在申请写锁时被阻塞。由于 MFT 搜索和基础导航不依赖该锁，所以它们还能动，但所有涉及“元数据面板”或“徽标更新”的操作都会感觉“卡死”。
- **计数卡在 0**：因为 `updateStatistics` 只有在初始化线程成功结束后才有机会由 `initializationFinished` 驱动执行。

### 2.2 信号风暴导致的定时器饥饿 (Timer Starvation)
这是导致“偶尔归零”的次要原因，但在死锁发生时加剧了现象。

- `CategoryPanel` 的刷新定时器设为 500ms。
- **缺陷**：任何 `metaChanged` 信号都会调用 `m_refreshTimer->start(500)`。
- 在 USN Journal 追平或后台视觉提取期间，信号流极其密集。如果信号间隔小于 500ms，定时器会**不断被重启**，导致其槽函数（负责拉取真实计数并填充 UI）永远无法执行。界面因此一直停留在第一阶段 `refresh()` 构建后的硬编码 `(0)` 状态。

### 2.3 架构确认：排除 WAL 膨胀风险
通过走读 `DatabaseManager.cpp`，确认系统采用 **SQLite In-Memory (:memory:)** 架构。
- 所有实时读写都在内存数据库中完成。
- 持久化是通过 `sqlite3_backup` 机制将内存库镜像到磁盘 `.db` 文件。
- 因此，不存在磁盘级 WAL 文件持续追加导致的读取变慢问题。

---

## 3. 详细修复方案

### 3.1 物理根除死锁 (最高优先级)
必须解除 `fullRecount` 过程中的锁升级行为。

**修改方案：**
- **方案 A (推荐)**：在 `forEachCachedItem` 外部，先将需要标记为 `Managed` 的路径收集到临时 `std::vector` 中，释放读锁后，再统一调用 `setManaged`。
- **方案 B**：在 `RuntimeMeta` 结构中增加 `isManaged` 的原子修改，跳过 `MetadataManager` 的全局大锁。

### 3.2 改造刷新计时器策略 (防止饥饿)
修改 `CategoryPanel::requestRefresh`，引入“非重启式防抖”。

**建议代码逻辑：**
```cpp
void CategoryPanel::requestRefresh(bool fullRebuild) {
    if (fullRebuild) {
        m_refreshTimer->setProperty("fullRebuild", true);
        m_refreshTimer->start(500); // 全量重建允许重启
        return;
    }
    
    if (!m_refreshTimer->isActive()) {
        m_refreshTimer->start(500); // 只有在不在计时时才启动
    }
}
```

### 3.3 优化初次构建占位符
修改 `CategoryModel::refresh()`，不再硬编码填充 `(0)`。改为直接读取 `CategoryRepo` 的 `s_totalFileCount` 等原子变量的当前值作为初始文本。

### 3.4 增强初始化鲁棒性
在 `MetadataManager::initFromScchMode` 中，将 `m_loaded = true` 的设置移至所有预处理（包括 `fullRecount`）完成之后，并增加超时监控。

---

## 4. 逻辑验证建议
1. **死锁模拟**：在 `fullRecount` 中手动打印日志，确认是否能执行到最后一行 `qDebug() << "[Recount] 盘点完成"`.
2. **锁竞争测试**：在卡顿时尝试右键修改一个文件的星级，观察控制台是否报“元数据更新超时”或无反应。
3. **压力测试**：注入 10 万个 USN 变更，观察侧边栏计数是否能在 1 秒内稳定更新，而不是一直卡在 0。

## 5. 预期效果
解决死锁后，系统“持续缓慢”的现象将消失，后台初始化能顺利追平。配合定时器策略优化，侧边栏计数将实现“秒级响应”与“启动即准”，彻底解决归零问题。