# 磁盘优先架构演进分析 —— Analysis_Modification_Plan-119.md

## 1. 任务背景
当前系统采用“内存数据库 (:memory:) + 内存镜像 (m_cache) + 退出时回写磁盘 (sqlite3_backup)”的三层架构。由于物理 IO 延迟主要集中在程序退出时的全量备份阶段，导致用户体验到明显的“关不掉”现象。本次任务旨在将数据流向彻底翻转，实现“磁盘实时落盘，内存实时同步”，从而达成秒退目标。

## 2. 问题定位
- **模块：DatabaseManager**
  - 函数：`loadDb` 内部使用了 `:memory:` 数据库并执行了全量载入逻辑。
  - 函数：`getMemoryDb` 语义上依然指向内存库。
- **模块：MetadataManager**
  - 机制：`debouncePersist` 导致变更在内存库中积压。
  - 数据流：当前逻辑是先改 `m_cache`，随后异步写内存 DB，退出时才写磁盘。
- **模块：TrayController**
  - 函数：`onQuitApp` 包含一个 `while (!DatabaseManager::instance().flushStep())` 循环，这是导致退出延迟的直接原因。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 先将数据插入到数据库然后更新内存 | 翻转 `MetadataManager` 操作顺序：DB 写入成功后再同步 `m_cache` | ✅ |
| 2    | 凡是与此不符的都必须做调整 | 废弃 `:memory:` 中转层，直接操作物理 `.db` 文件 | ✅ |
| 3    | 这样在退出的时候就可以秒推出了 | 移除 `TrayController` 中的 `flushStep` 同步循环 | ✅ |

## 4. 详细解决方案

### 4.1 DatabaseManager: 物理库直连化
- **废弃内存库**：修改 `loadDb`，不再开启 `:memory:` 库，也不执行 `sqlite3_backup` 载入动作（对应用户原话：“凡是与此不符的都必须做调整”）。`DbConnection` 结构中保留 `diskDb`，移除 `memDb`。
- **性能补强 (WAL)**：在打开物理数据库后，执行第一步操作是启用 `PRAGMA journal_mode=WAL;` 和 `PRAGMA synchronous=NORMAL;`（对应用户原话：“这样在退出的时候就可以秒推出了”）。这允许在不阻塞读操作的情况下进行高效写入。
- **接口语义对齐**：`getMemoryDb` 更名为 `getDiskDb`（或保持名称但内部返回磁盘句柄），确保 `MetadataManager` 直接操作磁盘（对应用户原话：“先将数据插入到数据库然后更新内存”）。

### 4.2 MetadataManager: 数据流向翻转
- **原子操作重构**：所有 `setRating`, `setTags`, `setColor` 等方法，修改流程顺序为：先执行磁盘库事务写入，成功后再执行第二步更新内存镜像 `m_cache`（对应用户原话：“先将数据插入到数据库然后更新内存”）。
- **移除异步积压**：废弃 `m_batchTimer` 和 `m_dirtyPaths` 机制。变更应由 UI 或后台线程即时触发落盘事务，不再留到退出时处理（对应用户原话：“在退出的时候就可以秒推出了”）。
- **启动加载优化**：由于不再需要从磁盘载入到内存库，`initFromScchMode` 只需负责从磁盘库构建 `m_cache` 索引，减少一次 SQLite 内部拷贝开销。

### 4.3 CategoryRepo: 事务一致性
- **事务范围缩放**：确保 `addItemToCategory` 和 `removeItemFromCategory` 内部的 `SqlTransaction` 直接作用于物理库句柄（对应用户原话：“先将数据插入到数据库然后更新内存”）。
- **计数器实时落盘**：`updatePersistentStat` 必须确保增量实时写入 `system_stats` 物理表。

### 4.4 退出逻辑：极简闭环
- **移除同步循环**：在 `TrayController::onQuitApp` 中，彻底移除 `flushStep` 相关的 `while` 同步循环（对应用户原话：“在退出的时候就可以秒推出了”）。
- **句柄安全关闭**：由于数据已实时落盘，退出时仅执行一次显式的 `DatabaseManager::shutdown()` 关闭物理句柄动作。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块：`DatabaseManager` (数据库初始化与句柄管理)
- [ ] 模块：`MetadataManager` (核心 Setter 与持久化顺序)
- [ ] 模块：`CategoryRepo` (分类关联实时落盘)
- [ ] 模块：`TrayController` (退出流程裁剪)

**明确禁止越界修改的范围：**
- [ ] 严禁修改物理表结构 (Schema)。
- [ ] 严禁移除 `m_cache` 内存镜像（UI 检索仍依赖 O(1) 的内存访问）。

## 6. 实现准则与预警【核心】
- **锁顺序风险**：在执行“写磁盘 -> 改内存”时，必须遵循“先磁盘 IO，后持锁改内存”的顺序，严禁在持有 `m_mutex` 写锁期间执行耗时的磁盘事务。
- **WAL 模式副作用**：WAL 模式会产生 `-wal` 和 `-shm` 临时文件。在 `shutdown` 时应确保正确关闭句柄以触发自动检查点（Checkpoint）和清理。
- **UI 响应性预警**：虽然 WAL 写入极快，但对于 500+ 项的大规模批量修改（如全量重置），必须使用 `SqlTransaction` 包装。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 数据库入库红线 | INSERT/REPLACE 仅由物理变动触发（针对托管库） | ✅ 保持一致，手动元数据修改作为唯一补充 |
| 窗口置顶/UI样式 | 遵循现有色值与 Win32 接口 | ✅ 无 UI 变动，符合规约 |
| 架构红线 6.1 | 范围感知 | ✅ 内存镜像检索逻辑不变，符合规约 |

## 8. 待确认事项
- **批量导入性能**：在 `registerItemsAsync` 大量入库时，是否需要采用“500 项一提交”的策略？（对应用户“秒退”目标，物理库必须处理好这种高频写入）。
