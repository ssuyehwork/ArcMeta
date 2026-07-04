# Analysis_Modification_Plan-131: 架构逻辑“去毒”与性能飞跃方案

## 1. 核心问题诊断
当前架构存在五大“傻逼”逻辑，直接导致了 I/O 冗余、CPU 空转及 UI 伪假死：
1. **冗余持久化链条**：直连磁盘模式下依然保留“同步写内存+异步写磁盘”的双重操作，导致锁竞争严重。
2. **低效 USN 过滤**：基于全路径字符串拼接进行托管库判定，将 $O(log N)$ 退化为高频 I/O 递归。
3. **状态翻转风暴**：`registerItem` 强行执行“0 -> 1”状态切换，在批量导入时产生翻倍的无效事务。
4. **计数器泄露**：异步任务计数器缺乏 RAII 保护，异常或空跳时导致 UI 状态永久卡死。
5. **上帝对象过载**：`MainWindow` 违规处理底层硬件消息与 IO 逻辑，导致逻辑耦合严重。

## 2. 具体修改方案

### 方案 A：废除冗余异步持久化 (Database Tier)
*   **动作**：重构 `MetadataManager::persistAsync`。
*   **细节**：
    *   移除 `DatabaseManager::instance().enqueueSyncTask(...)`。
    *   将原有的“内存库绑定逻辑”升级为“直连库事务提交”。
    *   在 `SqlTransaction` 包裹下一次性完成写入，利用 WAL 模式的并发特性。

### 方案 B：重构 FRN 判定链 (Filtering Tier)
*   **动作**：在 `AutoImportManager` 中引入 `ManagedFrnCache`。
*   **细节**：
    *   在启动或托管库变更时，记录托管根目录的 `FRN`。
    *   `isUnderManagedLibrary` 接收到信号后，调用 `MftReader` 获取该 FRN 的父级链。
    *   仅在内存中比对 FRN 节点，完全废弃 `getFullPath` 字符串拼接。

### 方案 C：引入“物理指纹”准入机制 (Logic Tier)
*   **动作**：改进 `MetadataManager::registerItem`。
*   **细节**：
    *   在执行 `updateIngestionStatus(0)` 前，对比磁盘文件的 `mtime` 和 `size`。
    *   若指纹一致且状态已为 1，则直接跳过后续所有解析与写入流程。
    *   仅对真正发生变化的文件执行解析流水线。

### 方案 D：落地 RAII 状态令牌 (Infrastructure Tier)
*   **动作**：定义 `struct SyncTaskToken`。
*   **细节**：
    *   构造函数：执行 `m_pendingTasksCount++`。
    *   析构函数：执行 `m_pendingTasksCount--` 并发射信号。
    *   在 `DatabaseManager` 的每一个 Lambda 任务中首行声明该令牌。

### 方案 E：MainWindow 职责剥离 (UI Tier)
*   **动作**：建立 `DeviceService` 专职处理硬件信号。
*   **细节**：
    *   将 `MainWindow` 中关于 `WM_DEVICECHANGE` 的处理逻辑迁移至 `CoreController`。
    *   由 `CoreController` 驱动 `MftReader` 的索引重建，通过信号通知 UI 刷新，而非 UI 主动干预。

## 3. 预期收益
*   **I/O 压力**：数据库写入频率降低约 50% - 70%。
*   **CPU 占用**：在大规模物理变动时，USN 过滤损耗降低 90% 以上。
*   **稳定性**：彻底根治 UI 同步计数器不归零的 Bug。
*   **架构清晰度**：解决“上帝对象”问题，提升代码可维护性。

## 4. 实施优先级
1. **方案 D** (稳定性基础)
2. **方案 A + C** (性能核心)
3. **方案 B** (极限优化)
4. **方案 E** (代码整洁)
