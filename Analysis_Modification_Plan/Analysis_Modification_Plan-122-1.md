# AutoImportManager 异步化与 UI 响应性能优化 —— Analysis_Modification_Plan-122.md

## 1. 任务背景
用户反馈在托管库（ArcMeta.Library_）执行大目录导入或实时监听变动时，UI 界面会出现频繁的“应用程序没有响应”假死现象。经排查，原因是 `AutoImportManager.cpp` 中的多处核心逻辑在主线程同步执行了耗时的物理磁盘扫描、图像元数据提取及数据库事务操作。

## 2. 问题定位
- **主线程同步点 A**：`onEntryAdded` / `onEntryUpdated` -> `handleRecursiveIngestion`。递归扫描一个含万级文件的目录时，主线程被完全占据。
- **主线程同步点 B**：`processImportQueue` 中的 `for` 循环同步调用 `MetadataManager::instance().registerItem()`。即使是单文件导入，`registerItem` 内部同步执行的图像主色提取（`tryExtractColor`）也是 CPU 密集型操作。
- **主线程同步点 C**：`handleRecursiveIngestion` 内部开启的 SQLite 事务（`BEGIN TRANSACTION`）和大量磁盘 I/O（`fetchWinApiMetadataDirect`）均在主线程执行。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | handleRecursiveIngestion 整体移入 QtConcurrent::run | 在 onEntryAdded/Updated 中改为异步启动 lambda | ✅ |
| 2    | processImportQueue 循环移入 QtConcurrent::run | 将 QTimer 触发的逻辑转交给后台线程执行 | ✅ |
| 3    | 保留并移入信号抑制与事务边界 | 确保 setInternalOperating 与 BEGIN/COMMIT 在后台线程配对 | ✅ |
| 4    | 确认信号通知最终在主线程触发 | 验证 MainWindow 使用 AutoConnection (默认行为) | ✅ |
| 5    | 检查数据库多线程安全性 | 评估 sqlite3 句柄并发访问风险并提供保护建议 | ✅ |
| 6    | 禁止修改 MetadataManager.cpp | 修复范围严格限定在 AutoImportManager.cpp | ✅ |

## 4. 详细解决方案

### 4.1 handleRecursiveIngestion 的异步封装
将 `handleRecursiveIngestion` 的整体执行体用 `QtConcurrent::run` 包裹为后台任务，从 `onEntryAdded` / `onEntryUpdated` 中改为异步派发（对应用户原话：“将 handleRecursiveIngestion() 的整体执行体……用 QtConcurrent::run 包裹为后台任务，从 onEntryAdded / onEntryUpdated 中改为异步派发”）。

**构建预警（C4858）**：由于 `QtConcurrent::run` 返回 `QFuture`，若不使用返回值会导致编译器警告。实现时必须通过 `(void)` 强制转换消除警告。

```cpp
// 伪代码思路
void AutoImportManager::onEntryAdded(uint64_t key) {
    // ... 前置路径解析逻辑保持不变 ...
    if (isManaged) {
        if (isDir) {
            // 异步派发递归扫描，(void) 用于消除 C4858 警告
            (void)QtConcurrent::run([this, fullPath]() {
                handleRecursiveIngestion(fullPath);
            });
        }
    }
}
```

### 4.2 processImportQueue 的异步化
将对 `registerItem` 的循环调用放入 `QtConcurrent::run` 的后台任务体内执行（对应用户原话：“processImportQueue() 中对 registerItem 的循环调用同样需要放入 QtConcurrent::run 的后台任务体内执行”）。

```cpp
void AutoImportManager::processImportQueue() {
    // 1. 在主线程中取出待处理路径（持有互斥锁）
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
    }
    if (pathsToProcess.empty()) return;

    // 2. 将耗时的 registerItem 循环移入后台线程，(void) 消除警告
    (void)QtConcurrent::run([this, pathsToProcess]() {
        // 分组处理逻辑（根据盘符/DB分区）
        for (const auto& path : pathsToProcess) {
            MetadataManager::instance().registerItem(path, true);
        }
    });
}
```

### 4.3 事务与信号抑制的闭环迁移
将 `setInternalOperating(true/false)` 信号抑制逻辑与 `BEGIN TRANSACTION / COMMIT` 事务边界完整地一并移入后台线程执行体（对应用户原话：“必须保留现有的 setInternalOperating(true/false) 信号抑制逻辑与 BEGIN TRANSACTION / COMMIT 事务边界，将它们完整地一并移入后台线程执行体”）。

执行顺序如下：
1. `MetadataManager::instance().setInternalOperating(true)`
2. `SqlTransaction trans(db);`（使用 RAII 确保事务在后台线程内闭环）
3. 执行循环或递归解析
4. `trans.commit();`
5. `MetadataManager::instance().setInternalOperating(false)`
6. `MetadataManager::instance().notifyFullUIRebuild()`

### 4.4 数据库线程安全加固方案
- **现状分析**：`DatabaseManager` 返回的是共享的 `memDb` 指柄。虽然 SQLite 默认开启了某些程度的保护，但多线程并发执行 `sqlite3_step` 会触发 `SQLITE_MISUSE` 或损坏。
- **保护措施**：
    - 由于 `MetadataManager` 本身在 `persistBatchAsync` 等操作中已经使用了 `DatabaseManager::instance().enqueueSyncTask`（串行化落盘），但内存库查询是并发的。
    - **方案建议**：在 `AutoImportManager` 的后台任务中，访问 `CategoryRepo` 或 `MetadataManager` 时，确保这些组件内部已具备锁保护。经考古，`MetadataManager` 内部已广泛使用 `std::shared_mutex m_mutex`。`CategoryRepo` 直接操作 DB，需在 `AutoImportManager` 调用处或其内部增加互斥保护。
    - **具体实现**：在 `AutoImportManager.cpp` 引入一个静态互斥锁 `s_dbAccessMutex`，专门保护后台导入任务对全局 DB 句柄的写入操作。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [x] 文件：`src/core/AutoImportManager.cpp` (核心逻辑异步化重构)

**明确禁止越界修改的范围：**
- [ ] 文件：`src/meta/MetadataManager.cpp` (严禁改动 `registerItem` 实现)
- [ ] 文件：`src/ui/MainWindow.cpp` (严禁手动修改信号连接类型，除非验证发现是 DirectConnection)

## 6. 实现准则与预警【核心】

1.  **头文件依赖**：必须包含 `<QtConcurrent>` 和 `<QFuture>`。
2.  **事务配对预警**：在后台线程中，若 `BEGIN TRANSACTION` 之后发生异常或中途退出，必须确保 `ROLLBACK` 或 `COMMIT` 被触发，否则会锁死数据库句柄。建议使用 RAII 风格的 `SqlTransaction`（若项目中有此组件，经考古 `src/meta/DatabaseManager.cpp` 中确实存在 `SqlTransaction` 类，应优先复用）。
3.  **UI 线程安全**：`notifyFullUIRebuild()` 最终会通过 `MetadataManager` 的 `m_uiSignalTimer` 触发，该 Timer 在主线程创建，故 `QTimer::start` 必须通过 `QMetaObject::invokeMethod` 跨线程调用。经审计 `MetadataManager.cpp` 第 202 行已实现此机制。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 异步模型 | 耗时扫描与 I/O 必须脱离主线程执行 | ✅ 符合 |
| 信号抑制 | 操作期间拦截冗余刷新信号（setInternalOperating） | ✅ 符合 |
| 事务管理 | BEGIN/COMMIT 必须在同一线程配对 | ✅ 符合 |
| 作用域对齐 | 仅处理托管库（ArcMeta.Library_）内数据 | ✅ 符合 |

## 8. 待确认事项
1.  **SqlTransaction 复用**：`DatabaseManager.h` 中的 `SqlTransaction` 是否允许跨文件复用？（经审计代码，其定义在 `DatabaseManager.h` 的命名空间内，可在 `AutoImportManager.cpp` 中直接使用）。
2.  **信号接收方检查**：经审计 `src/ui/MainWindow.cpp`，`metaChanged` 信号的连接未指定 `Qt::DirectConnection`，确认符合 `AutoConnection` 预期，无须修改 UI 代码。
