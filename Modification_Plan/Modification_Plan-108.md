# SyncStatusService 全局多源任务聚合器重构方案 —— Modification_Plan-108.md

> 状态：已批准，执行中

## 1. 任务背景
在目前的版本中，主界面底部的极光同步进度条会发生“闪退式”假完成现象。

究其技术根源，是因为 `SyncStatusService` 仅仅订阅了 `DatabaseManager`（SQLite 数据库落盘任务），完全没有感知文件耗时扫描和 `MediaExtractorPipeline` 后台多媒体特征提取（图片尺寸/调色盘计算）这两个真正长周期的任务。当数据库几毫秒写完后，`pendingCount` 瞬间清零，进度条便直接归零消失，而真正的耗时后台线程才刚刚开始工作。

本方案旨在重构并升级 `SyncStatusService` 为工业级的**“全局多源任务聚合器”**，完美聚合“数据库写盘 + 多媒体特征提取 + 扫描注册”三方后台池总量，消除进度假消失，展示精准同步进度。

## 2. 问题定位
1. **数据感知渠道狭隘**：`SyncStatusService` 仅连接了 `DatabaseManager` 的 `pendingTasksCountChanged`，在其他耗时线程开始运行时毫无感应。
2. **多线程提取进程失联**：多媒体特征解析运行于 `QtConcurrent::run` 中，由于其进出没有对应的状态上报，UI 无法展现真实的进行百分比。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | SyncStatusService 重构为多源任务聚合器 | 统一声明 `m_dbPending`、`m_mediaPending`、`m_scanPending`，返回三者累加之和。 | ✅ |
| 2    | 通知多媒体队列进度 | 在 `MediaExtractorPipeline.cpp` 队列变化和每个 item 处理完后，将排队和正在处理总量推给 `SyncStatusService`。 | ✅ |

## 4. 详细解决方案

### 4.1 全局多源任务聚合器 `SyncStatusService`
- **成员更新**：
  在 `SyncStatusService` 内变更为 3 个原子的 `std::atomic<int>`：数据库写盘数（`m_dbPending`）、特征待提取数（`m_mediaPending`）和文件待注册数（`m_scanPending`）。
- **槽函数扩展**：
  新增 `updateDbPending(int)`、`updateMediaPending(int)`、`updateScanPending(int)`，当任何一个任务源的值变更时，自动进行 150ms 窗口节流并分发统一的 `statusUpdated(syncing, pendingCount)` 信号。

### 4.2 多媒体特征提取管线 `MediaExtractorPipeline` 的精细上报
- **计数器添加**：
  在 `MediaExtractorPipeline` 中定义 `std::atomic<int> m_activeCount{0}`，用以追踪目前正在 `QtConcurrent` 线程池中被并行解析的多媒体项数量。
- **动态推进计算**：
  - 当调用 `enqueue(path)` 或 `enqueueBatch(paths)` 时，更新 `m_queue.size()` 给 `SyncStatusService`。
  - 当 `processNextBatch()` 启动后台线程消费时，将 `batch.size()` 叠加给 `m_activeCount`。
  - 在 `processItemDirect`（或每次特征处理完结）时，执行 `m_activeCount.fetch_sub(1)` 递减，并在锁保护下向 `SyncStatusService` 实时投递 `m_queue.size() + activeCount`，使进度条以 1 个项为最小单位平滑右推拉满。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/core/SyncStatusService.h` （声明任务多源及相关原子计数和槽方法）
- [ ] 模块/文件：`src/core/SyncStatusService.cpp` （实现全局聚合计算 pendingCount 与节流 notify）
- [ ] 模块/文件：`src/meta/MediaExtractorPipeline.h` （追加 `m_activeCount` 成员变量声明，导入 `<atomic>`）
- [ ] 模块/文件：`src/meta/MediaExtractorPipeline.cpp` （实现 enqueue 与 item 完成时的动态计数回传）

**明确禁止越界修改的范围：**
- [ ] 各项数据多媒体提取的直接解析算法——不修改
- [ ] SQLite 核心落盘逻辑——不修改

## 6. 实现准则与预警【核心】
1. **线程与原子安全性**：各任务源的上报来自于高频不同的工作线程，必须在锁保护或原子操作下（如 `std::atomic`）执行，确保其数据状态的绝对正确。
2. **多线程防死锁**：在 `MediaExtractorPipeline` 触发 `updateMediaPending` 时，必须小心不引起与 `m_queueMutex` 的双向死锁，各模块应安全读取数值。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 路径标准化  | 无路径操作，不涉及。 | ✅ |

## 8. 待确认事项（可选）
- 暂无
