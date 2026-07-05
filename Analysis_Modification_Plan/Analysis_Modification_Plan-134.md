# Analysis_Modification_Plan-134: 跨表一致性与启动并发加固方案

本方案旨在解决架构重构后出现的隐蔽逻辑冲突，特别是针对数据库单轨化后的多表操作一致性问题。

## 1. 彻底根除 CategoryRepo 冗余异步逻辑 (Repository Tier)

### 1.1 现状分析
`CategoryRepo` 中的 `addItemToCategory` 和 `removeItemFromCategory` 依然在调用 `DatabaseManager::instance().enqueueSyncTask`。

### 1.2 风险
在磁盘直连模式下，这会导致针对同一物理句柄的高频并发锁竞争，引发 `SQLITE_BUSY`。

### 1.3 修正方案 (src/meta/CategoryRepo.cpp)
*   **动作**：移除所有 `enqueueSyncTask` 闭包代码。
*   **逻辑**：
```cpp
// 示例修改
if (sqlite3_step(memStmt) == SQLITE_DONE) {
    sqlite3_finalize(memStmt);
    // 移除原有的异步磁盘分发块
    syncCategorizedCountForFid(fileId128); // 此时已真实入库
    return true;
}
```

## 2. 抑制启动阶段的“进度计算风暴” (Concurrency Tier)

### 2.1 现状分析
启动对账（`fullRecount`）会触发成千上万次 `registerItem`，每个注册又会抛出一个线程池任务去算目录进度。

### 2.2 风险
海量 I/O 任务挤占线程池，导致数据库锁死，UI 初始化极慢。

### 2.3 修正方案
*   **动作**：在 `MetadataManager` 中增加 `m_isRecounting` 标志位。
*   **逻辑**：
    在 `fullRecount` 开始时置 true。`updateIngestionStatus` 内部判断：`if (!m_isRecounting) { ... 触发异步进度计算 ... }`。
    对账结束后，执行一次性的全局进度对账。

## 3. 增强型事务支持 (Database Tier)

### 3.1 现状分析
目前的 `SqlTransaction` 仅通过 `autocommit` 躲避嵌套。

### 3.2 方案建议
*   **重构**：将 `BEGIN TRANSACTION` 升级为 `SAVEPOINT` 机制，以支持真实的逻辑嵌套回滚。

## 4. USN 信号“假阴性”补救 (USN Tier)

### 4.1 方案建议
*   **逻辑**：在 `AutoImportManager` 中增加“重试判定队列”。
*   **细节**：如果 `getFullPath` 返回为空，将该 FRN 放入一个 500ms 后执行的延迟判定队列，给 MFT 索引构建留出同步窗口。

## 5. 实施约束
*   **禁止**：在 `CategoryRepo` 的静态方法中执行任何耗时的 `std::vector` 全量拷贝，优先返回引用或智能指针。
*   **要求**：所有 `CategoryItem` 的 FID 关联操作必须强制校验 `isFolder` 标志位，严禁将文件夹关联至普通分类。
