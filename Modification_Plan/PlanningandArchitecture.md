# Planning and Architecture —— PlanningandArchitecture.md

本文件旨在如实呈现 ArcMeta 客户端本仓库实际存在的完整运行流程、线程模型、数据库读写机制与关键交互调用链，不进行任何美化或主观假设。本文档完全基于实际的代码走查结果，旨在为后续排查界面卡顿、假死、锁竞争、线程竞争提供精确且独立的排查依据。

---

## 一、应用启动流程

应用从程序主入口 `main()` 函数启动，逐步初始化核心业务单例并渲染显示主窗口。具体物理执行步骤如下：

### 1.1 程序入口与单实例锁检验
*   **文件与函数**：`src/main.cpp` - `main(int argc, char *argv[])`
*   **执行线程**：主线程（UI 线程）
*   **同步/异步**：同步执行
*   **执行逻辑**：
    1.  检测单实例锁（Windows 下通过 Win32 API `CreateMutexA` 建立全局互斥锁 `ArcMeta_SingleInstance_Mutex`，非 Windows 系统通过 `QLockFile` 在临时目录创建锁文件）。若已存在则直接返回 `0` 退出。
    2.  调用 Win32 原生 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` 初始化 COM 环境（用于后续多媒体提取与 Shell 缩略图生成）。
    3.  通过 `qInstallMessageHandler(customMessageHandler)` 注册自定义日志处理器，日志落盘时使用静态 `QMutex s_logMutex` 保护物理写入。
    4.  设置 `QPalette` 的高亮颜色，设置高 DPI 缩放策略。

### 1.2 核心业务单例主线程预热（物理预热点）
*   **文件与函数**：`src/main.cpp` - `main(...)`
*   **执行线程**：主线程（UI 线程）
*   **同步/异步**：同步执行
*   **执行逻辑**：
    1.  调用 `ArcMeta::MetadataManager::instance()` 初始化元数据管理器单例。
    2.  调用 `ArcMeta::CategoryRepo::initialize()` 初始化分类仓库层（当前在 SQLite 模式下为空实现，其内部实际依赖 `DatabaseManager` 在后续进行加载）。
    *   *说明*：此步在主线程预热的目的是确保单例内部包含的 `QTimer` 等对象能够正确归属于主线程，避免后续跨线程创建引发 Qt 运行时警告或未定义行为。

### 1.3 UI 界面装载与首帧渲染
*   **文件与函数**：`src/main.cpp` -> `MainWindow::MainWindow(QWidget* parent)` -> `MainWindow::initUi()`
*   **执行线程**：主线程（UI 线程）
*   **同步/异步**：同步执行
*   **执行逻辑**：
    1.  执行 `MainWindow::initUi()` 创建并组装六面板布局（`CategoryPanel`、`NavPanel`、`ContentPanel` 等）和导航工具栏。
    2.  构造系统托盘控制器 `TrayController`，创建 hover 与 resize 事件过滤器并为窗口 viewport 安装。
    3.  在 `main.cpp` 中调用 `w->show()`，渲染展示主窗口首帧。
    *   *说明*：**此步不加载任何缓存数据，亦无数据库查询，UI 构造零延迟呈现**。所有的面板数据加载均为延迟加载或启动后异步触发。

### 1.4 后台异步扫描与缓存对账系统点火（启动核心）
*   **文件与函数**：`src/main.cpp` -> `ArcMeta::CoreController::instance().startSystem()`
*   **执行线程**：主线程触发 -> 全局线程池 `QThreadPool::globalInstance()`
*   **同步/异步**：异步启动
*   **执行逻辑**：
    1.  `main()` 函数在 `w->show()` 之后，同步调用 `CoreController::startSystem()`。
    2.  `startSystem()` 内部向全局线程池投递一个 lambda 任务。
    3.  在后台线程池线程中执行：
        *   在主窗口上触发 UI 状态更新：发送信号通知主线程 `setStatus("正在载入元数据缓存...", true)`（通过 `QMetaObject::invokeMethod` 跨线程安全投递）。
        *   调用 `MetadataManager::instance().initFromScchMode()` 开始加载磁盘分库。
        *   `initFromScchMode()` 内部同步调用 `DatabaseManager::instance().init()` 建立数据库连接（详情见第三节）。
        *   调用 `loadFromDb` 读取全局数据库 `global.db` 的 `metadata` 和 `system_stats`。
        *   遍历并扫描程序目录 `.arcmeta/` 下的所有物理磁盘分库（形如 `Arcmeta_*.db`），并针对是在线磁盘的卷序列号，调用 `DatabaseManager::instance().getMemoryDb()` 打开连接并读取其 `metadata` 存量数据，将其全部加载并缓存至内存映射 `MetadataManager::m_cache` 及对应的倒排索引映射中。
        *   调用 `CategoryRepo::fullRecount()` 执行全量账本对账，加载并同步 `system_stats` 内存储的统计计数。
        *   在 `fullRecount()` 内部，异步启动一个 `QtConcurrent::run` 后台校验任务，逐个盘点内存缓存项是否存在于物理磁盘或 FRN 是否匹配，若已被第三方删除，则调用 `MetadataManager::setInvalid` 标记失效。
        *   在对账结束后，调用 `DatabaseManager::instance().flushAll()` 物理落盘。
    4.  调用 `AutoImportManager::instance().startListening()` 开始对托管库进行变化侦听（2026-08-xx 后已注销 USN Journal 变更监听，全面转向 NativeFolderWatcher IOCP 监控）。
    5.  利用 `QMetaObject::invokeMethod` 异步发出通知给主线程：`setStatus("系统就绪", false)` 并发射 `initializationFinished()` 信号，通知 UI 刷新显示内容，异步加载正式结束。

---

## 二、模块与线程清单

### 2.1 应用存在的所有线程清单

| 线程名称/标识 | 类型 | 创建位置与驱动机制 | 线程用途与职责 | 生命周期 |
| :--- | :--- | :--- | :--- | :--- |
| **UI 主线程** | 主线程 | 操作系统/程序入口启动 | 处理重绘事件、鼠标键盘交互、定时器触发、GUI 控件布局分配及槽函数同步执行。 | 常驻进程级别 |
| **Database Sync Worker** | 后台工作线程 | `DatabaseManager::startWorkerThread()`，内部由 `std::thread m_workerThread` 驱动。 | 驱动顺序执行 `m_syncQueue` 中的数据库写入/异步同步任务，执行 `getDiskDb` 对应的磁盘文件高 I/O 操作。 | 常驻，直到 `DatabaseManager` 析构 |
| **MFT/USN Monitor Thread** | 后台工作线程 | `UsnWatcher::watcherLoop()`，`std::thread` 驱动。 | 监听系统底层的 USN Journal 磁盘扇区变化事件，捕捉第三方文件删除/迁移。 | 常驻，直到监听关闭 |
| **IOCP Monitor Thread** | 后台工作线程 | `NativeFolderWatcher` 内部 Win32 IOCP 线程。 | 监听托管库与自定义文件夹的物理变化。 | 常驻，直到监控移除 |
| **Concurrent Thread Pool** | 线程池 | `QThreadPool::globalInstance()` / `QtConcurrent::run()`。 | 执行耗时的目录递归扫描（`ContentPanel::loadDirectory` 后台扫描）、视觉颜色提取（`UiHelper::extractPalette`）、图像尺寸读取以及大容量数据批量落盘事务。 | 临时，按需唤醒与销毁 |

### 2.2 NativeFolderWatcher (IOCP) 监控目录映射机制

经过对 `CoreController::startSystem()` 和 `MainWindow::showNewAutoImportDialog()` 的代码走查，`NativeFolderWatcher (IOCP)` 机制主要监控以下两类物理文件夹：

1.  **分区的托管库文件夹（Managed Library Folders）**：
    *   *扫描触发与位置*：启动时（`src/core/CoreController.cpp` 第 56-68 行），系统遍历物理驱动器。如果磁盘包含有效卷序列号（`volSerial != L"UNKNOWN"`），系统会通过 `MetadataManager::getManagedLibraryPath(volSerial, letter)` 提取该分区对应的常驻托管库文件夹物理路径（如位于 `D:/ArcMeta.Library_D/`）。
    *   *监控建立*：调用 `NativeFolderWatcher::instance().addWatch(managedAbsW)` 对每个在线磁盘分区的托管库建立 IOCP 句柄监听。
2.  **用户自定义监控文件夹（Custom Monitored Folders）**：
    *   *启动时加载*：启动时（`src/core/CoreController.cpp` 第 70-80 行），系统读取持久化配置 `AppConfig` 的 `"DriveBar/CustomMonitoredFolders"` 键名，提取用户先前添加的所有自定义监控物理目录。对每项标准路径，同步调用 `addWatch(normPath)` 启动 IOCP 变化监听，并在后台线程对该目录执行递归扫描入库（`AutoImportManager::handleRecursiveIngestion`）。
    *   *动态新增监控*：当用户通过界面盘符栏空白右键菜单动态添加自动导入文件夹时（`src/ui/MainWindow.cpp` 第 1805-1825 行），在用户选定目标物理路径后，系统不仅将其持久化到 `AppConfig`，还会直接调用 `NativeFolderWatcher::instance().addWatch(normPath)` 即时动态点火激活对该自定义文件夹的 IOCP 变化监控。

### 2.3 所有跨线程通信（IPC）机制点

#### A. 信号与槽（Signals / Slots）
*   **`CoreController::initializationFinished` 信号**
    *   *发射源*：后台线程池初始化线程（`CoreController::startSystem()` 内部 lambda）。
    *   *接收槽*：主线程（`MainWindow`）。
    *   *连接类型*：`Qt::QueuedConnection`（自动跨线程解析投递）。
    *   *用途*：通知 UI 初始化就绪，开始加载首屏分类树和内容面板。
*   **`ContentPanel::directoryStatsReady` 信号**
    *   *发射源*：线程池异步工作线程（`ContentPanel::recalculateAndEmitStats` 内部 lambda）。
    *   *接收槽*：主线程（`FilterPanel::onDirectoryStatsReady`）。
    *   *连接类型*：`Qt::QueuedConnection`。
    *   *用途*：回传大型目录物理扫描后的 Rating、颜色、文件后缀引用计数分布，刷新高级筛选器。

#### B. `QMetaObject::invokeMethod` 机制（UI 异步投递）
*   **模型数据回传**
    *   *文件与位置*：`src/ui/ContentPanel.cpp` 中的 `loadDirectory` 后台扫描线程。
    *   *调用方式*：
        ```cpp
        QMetaObject::invokeMethod(QCoreApplication::instance(), [panelPtr, path, allItems, reqId]() { ... }, Qt::QueuedConnection);
        ```
    *   *用途*：工作线程扫描物理目录产生的 `std::vector<ItemRecord>`，被异步安全地导回主线程并填充至 `FerrexVirtualDbModel`，从而触发视图重绘。

#### C. 共享变量 + 锁
*   **`DatabaseManager::enqueueSyncTask` 投递队列**
    *   *机制*：使用 `std::mutex m_queueMutex` 配合 `std::condition_variable m_queueCv`。
    *   *通信方向*：多线程（主线程/工作线程）-> Database Sync Worker 线程。
    *   *用途*：主线程向后台 I/O 工作队列异步抛送同步持久化任务。

---

### 2.3 全局锁清单（QMutex / std::shared_mutex 等）

#### A. 元数据管理器读写共享锁
*   **锁对象**：`std::shared_mutex m_mutex`（声明于 `src/meta/MetadataManager.h`，定义于 `MetadataManager.cpp`）
*   **锁定资源**：`MetadataManager::m_cache` 映射容器、`m_fidToPath` 映射、`m_parentToChildren` 层级索引、所有按卷名及后缀排序的倒排索引映射。
*   **持锁期间的操作**：
    *   **读锁（`std::shared_lock`）**：在主线程 `getMeta()` 检索属性、`hasChildrenInCache()` 检查子项、`getChildrenFromCache()` 读取子项列表、`searchInCache()` 执行匹配时加锁。操作期间仅进行内存 Map 的并发查询，不涉及 I/O。
    *   **写锁（`std::unique_lock`）**：在 `setRating()`、`setColor()`、`setTags()`、`registerItem()` 等写入端加锁。持锁期间直接修改内存 Map 的值，并触发 `persistAsync()` 去抛送异步数据库同步任务。
    *   **死锁警报风险**：在 `forEachCachedItem`（持有读锁）回调函数内部，若回调逻辑试图调用带 `unique_lock` 的写 API（如同步调用 `setManaged`），将导致主线程与自身持有锁死锁（`shared_mutex` 不可重入）。

#### B. 数据库分卷连接映射锁
*   **锁对象**：`std::mutex m_mutex`（声明于 `src/meta/DatabaseManager.h`）
*   **锁定资源**：分卷数据库连接状态映射 `DatabaseManager::m_driveDbs` 及全局共享连接 `m_globalDb`。
*   **持锁期间的操作**：
    *   在 `getMemoryDb()` 中，持锁同步查找已加载的分卷，并在发生盘符漂移时，同步执行 `saveDb`、调用 `sqlite3_close_v2` 关闭句柄、通过 `QFile::rename` 迁移物理数据库文件，最后调用 `loadDb` 重新打开直连。
    *   在 `shutdown()` 中，持锁调用 `sqlite3_close_v2` 关闭所有活动的磁盘连接句柄。

#### C. 文件图标加载互斥锁
*   **锁对象**：`static QMutex fileIconMutex` 与 `static QMutex iconMutex`（声明于 `src/ui/UiHelper.h`）
*   **锁定资源**：静态文件图标缓存容器 `s_fileIconCache` 与 SVG 渲染缓存。
*   **持锁期间的操作**：由于 `QFileIconProvider`、`QIcon` 及底层 Windows Shell 提取 API（`SHGetFileInfo`）在多线程并发访问时非线程安全，因此在 `FerrexVirtualDbModel` 后台提取非图形文件图标时，必须通过 `QMutexLocker` 锁定。持锁期间在 `s_fileIconCache` 中查找、插入图标，从而使后台多线程文件图标的查询事实上被强制串行化。

---

## 三、数据库访问点清单

当前应用已经废除 `内存数据库 :memory:`，全面转向直连磁盘的高性能数据库模式（WAL 模式 + SYNCHRONOUS = NORMAL），所有的 `sqlite3*` 句柄均直接操作磁盘文件。

### 3.1 核心句柄 `getGlobalDb()` 与 `getMemoryDb()` 的底层 `sqlite3_open_v2` 调用细节

经过对 `src/meta/DatabaseManager.cpp` 中的 `loadDb` 函数的静态走查，这两个函数对应连接句柄的底层创建事实如下：

1.  **传入的文件路径参数具体是什么**：
    *   **`getGlobalDb()`**：其底层的数据库路径是通过 `DatabaseManager::init()` 中的 `globalPath` 决定，即 `getAppDir() + "/.arcmeta/global.db"` 的标准化路径（`std::wstring` 转换为 utf-8 的 std::string）。
    *   **`getMemoryDb()`**：其底层的数据库路径是每一个在线物理磁盘对应的本地分库数据库文件路径，具体形式为 `getAppDir() + "/.arcmeta/Arcmeta_<SERIAL>_<LETTER>.db"`。
    *   *代码事实*：两个函数传入 `sqlite3_open_v2` 的第一参数**全部为本地真实的磁盘文件路径**，没有使用 `":memory:"` 或 `"file::memory:?cache=shared"` 的内存或共享内存路径。
2.  **传入的 flags 参数具体是什么**：
    *   *代码事实*：在 `loadDb` 的底层调用中，`sqlite3_open_v2` 的第三个参数（flags）显式硬编码为：
        ```cpp
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
        ```
        并未开启如 `SQLITE_OPEN_SHAREDCACHE`（共享缓存）或 `SQLITE_OPEN_NOMUTEX` 等扩展 flags 标志。
3.  **连接对象的生命周期与线程共享模式**：
    *   *代码事实*：
        *   数据库连接句柄在 `DatabaseManager` 单例内部的私有成员变量 `m_globalDb` 和私有映射容器 `m_driveDbs`（`std::map<std::wstring, DbConnection>`）中唯一常驻维护。
        *   在任何调用线程（包括 UI 主线程、工作线程、监控线程和线程池线程）通过调用 `getGlobalDb()` 或 `getMemoryDb()` 获取底层 `sqlite3*` 连接句柄时，都是直接返回单例内部保存的**这同一份唯一连接句柄指针**。
        *   即**全进程全局/各磁盘分库分别仅持有唯一的一份连接句柄实例，多线程调用时共享同一份指针，并没有为各调用线程各自独立打开、创建或维护独立的连接对象**。

### 3.2 数据库访问点明细表

数据库访问点如下表所示：

| 文件名 + 函数名 | SQL 执行类型 | 是否在事务中 | Prepared Statement 机制 | 执行线程 | 连接对象模式 | SQL 具体业务用途 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`DatabaseManager.cpp`**<br>`SqlTransaction::SqlTransaction` | **开启事务** | 是 (事务入口) | 同步执行，每次现场拼字符串并执行 `BEGIN TRANSACTION` | 主线程 / 异步工作线程 | 全局或分卷直连句柄 | 建立 RAII 事务保护锁屏 |
| **`DatabaseManager.cpp`**<br>`loadDb` | **DDL 建表、索引** | 否 | 每次现场拼字符串（schema 脚本），调用 `sqlite3_exec` 批量建立并升级表结构 | 后台初始化线程 / 主线程 | 新打开的直连句柄 | 初始化或动态迁移数据库表、PRAGMA WAL 配置与字段 Alter 扩容 |
| **`CategoryRepo.cpp`**<br>`getAll` | **查询 (SELECT)** | 否 | 每次调用现场 prepare，执行后立即调用 `sqlite3_finalize` 析构 | 主线程 / 异步工作线程 | 全局共享句柄 `getGlobalDb()` | 获取所有系统及用户分类定义列表 |
| **`CategoryRepo.cpp`**<br>`add` | **插入 (INSERT)** | 否 | 每次调用现场 prepare 并执行 bind，使用后 `sqlite3_finalize` | 主线程 | 全局共享句柄 `getGlobalDb()` | 动态创建新的用户分类节点 |
| **`CategoryRepo.cpp`**<br>`update` | **更新 (UPDATE)** | 否 | 每次调用现场 prepare 并执行 bind，使用后 `sqlite3_finalize` | 主线程 | 全局共享句柄 `getGlobalDb()` | 修改已有分类属性（名称、色值、图标、加密密码、置顶） |
| **`CategoryRepo.cpp`**<br>`remove` | **删除 (DELETE)** | 是 | 多次现场 prepare 嵌套，包含 `category_items` 和 `categories` 清理，外层包装 `SqlTransaction` | 主线程 | 全局共享句柄 `getGlobalDb()` | 递归收集子分类并物理删除相应分类记录 |
| **`CategoryRepo.cpp`**<br>`addItemToCategory` | **插入 (INSERT)** | 否 | 现场 prepare。主线程在 `memDb` 执行完成后，向 `DatabaseManager` 队列投递异步 lambda，在 `m_workerThread` 线程中获取 `diskDb` 重新 prepare 执行 | 主线程在内存写入 + 后台同步线程在磁盘执行 | 全局共享句柄 `getGlobalDb()` | 建立 File ID 与指定分类 ID 的关联关系 |
| **`CategoryRepo.cpp`**<br>`removeItemFromCategory` | **删除 (DELETE)** | 否 | 现场 prepare。在主线程 `memDb` 执行完成后，向 `DatabaseManager` 异步分发在 `m_workerThread` 重复执行 | 主线程写入 + 后台同步线程执行 | 全局共享句柄 `getGlobalDb()` | 物理断开指定分类下的项目关联关系 |
| **`CategoryRepo.cpp`**<br>`getItemsInCategories` | **查询 (SELECT)** | 否 | 现场根据传入的 ID 数量拼接 IN 占位符字符串，执行 prepare 与 finalize | 主线程 / 异步工作线程 | 全局共享句柄 `getGlobalDb()` | 提取包含在指定分类列表中的所有文件关联记录 |
| **`CategoryRepo.cpp`**<br>`getCounts` | **查询 (SELECT)** | 否 | 现场 prepare，包含 `category_items` 与 `categories` 的关联查询，独立 finalize | 异步工作线程 | 全局共享句柄 `getGlobalDb()` | 批量盘点内存缓存中各用户分类对应的有效 FID 总数 |
| **`CategoryRepo.cpp`**<br>`updatePersistentStat` | **更新 (UPSERT)** | 否 | 每次调用现场 prepare，执行后向后台同步线程 `DatabaseManager` 派发在 `diskDb` 上重复 prepare 写入 | 主线程 / 后台同步线程 | 全局共享句柄 `getGlobalDb()` | 递增或递减账本统计值（全部数据、已分类数据） |
| **`CategoryRepo.cpp`**<br>`fullRecount` | **查询/更新** | 是 | 现场多次 prepare。包含在 `system_stats` 的 SELECT、以及用 `SqlTransaction` 包裹的 UPSERT 写入 | 主线程 | 全局共享句柄 `getGlobalDb()` | 物理对账，重计全局已分类/未分类存量数据并强制落盘 |
| **`CategoryRepo.cpp`**<br>`getRecentlyUsed` | **查询 (SELECT)** | 否 | 现场 prepare，包含 category 与关联项的关联 MAX 查询，用后 finalize | 异步工作线程 | 全局共享句柄 `getGlobalDb()` | 拉取最近活跃的 15 个分类定义 |
| **`MetadataManager.cpp`**<br>`initFromScchMode` | **查询 (SELECT)** | 否 | 多次现场 prepare。包含 `SELECT * FROM metadata` 与 `SELECT value FROM system_stats WHERE key LIKE 'PROGRESS:%'` | 后台初始化线程 | 全局共享及所有活动磁盘连接 | 启动加载元数据，装填内存镜像 |
| **`MetadataManager.cpp`**<br>`persistAsync` | **插入/更新** | 否 (单条执行) | 每次调用时根据 `isNew` 现场 prepare 对应的 INSERT 或 UPDATE 语句。 | 主线程 / 后台线程池 (IOCP/MFT) | 各卷磁盘直连连接对象 | 物理文件指纹与修改后的 Rating/Color 属性落盘 |
| **`MetadataManager.cpp`**<br>`persistBatchAsync` | **插入/更新** | 是 | 优化性能：外层加 `SqlTransaction` 事务保护，在循环中对每个路径现场 prepare SQL、bind、step 并 finalize | 后台线程池 (IOCP/MFT) | 各卷磁盘直连连接对象 | 大规模文件导入/对账时，大批次高性能写入磁盘 |
| **`MetadataManager.cpp`**<br>`removeMetadataSync` | **删除 (DELETE)** | 否 | 现场 prepare `DELETE FROM metadata WHERE file_id = ?`，执行后析构 | 主线程 / 后台同步线程 | 各卷磁盘直连连接对象 | 物理删除单个文件的元数据属性 |
| **`MetadataManager.cpp`**<br>`removeMetadataBatchSync` | **删除 (DELETE)** | 是 | 优化大批量删除：外层加 `SqlTransaction` 事务，循环中同步 prepare 并执行删除 SQL | 后台线程池 | 各卷磁盘直连连接对象 | 批量安全抹除文件或文件夹删除时，彻底清理数据库记录 |
| **`TagManagerView.cpp`**<br>`onClassifyTag` / `onDeleteTag`等 | **CRUD** | `BEGIN TRANSACTION` (手动 exec) | 现场 prepare、bind、step 并立即 finalize。多步骤写入使用手动 exec 拼接 `BEGIN/COMMIT` 事务。 | 主线程 | 全局共享句柄 `getGlobalDb()` | 标签组创建、标签名关联管理、标签组级联删除 |

---

## 四、关键交互流程

### 4.1 打开应用并异步预热缓存
1.  `[main.cpp:main]`：程序在主线程启动（同步）。
2.  `[MainWindow.cpp:MainWindow]`：主窗口构造，执行 `initUi()`。不产生任何加载延迟（同步，主线程）。
3.  `[main.cpp:main]`：调用 `MainWindow::show()`，主界面首帧无延迟呈现在屏幕上（同步，主线程）。
4.  `[main.cpp:main]` -> `[CoreController.cpp:startSystem]`：主线程向后台线程池投递初始化任务（异步，主线程 -> 线程池）。
5.  `[MetadataManager.cpp:initFromScchMode]`：后台工作线程启动，调用 `DatabaseManager::init()` 初始化连接，读取分库将元数据加载入内存（同步，工作线程）。
6.  `[CategoryRepo.cpp:fullRecount]`：后台调用物理校验，派发 `QtConcurrent::run` 盘点无效文件（异步，工作线程 -> 线程池）。
7.  `[MetadataManager.cpp:notifyUI]` -> `[CategoryModel.cpp:refresh]`：初始化完毕，主线程接收到 Queued 信号，侧边栏分类树执行重置并显示系统和自定义分类树（异步通信，工作线程 -> 主线程）。

### 4.2 切换列表筛选条件（FilterPanel 选择）
1.  `[FilterPanel.cpp:onCheckboxClicked]`：用户勾选星级、颜色、文件类型（同步，主线程）。
2.  `[FilterPanel.cpp:filterChanged]`：发射 `filterChanged(FilterState state)` 信号（同步，主线程）。
3.  `[ContentPanel.cpp:applyFilters]`：主窗口将状态同步到 `ContentPanel`，调用 `applyFilters(state)`（同步，主线程）。
4.  `[ContentPanel.cpp:applyFilters]` -> `[FilterProxyModel::updateFilter]`：调用 `beginFilterChange()` 和 `endFilterChange()` 通知关联视图开始筛选（同步，主线程）。
5.  `[ContentPanel.cpp:FilterProxyModel::filterAcceptsRow]`：代理模型过滤函数对模型中的每一行记录在主线程中进行过滤判定，判定逻辑直接从内存 `m_allRecords` 中检索属性（同步，主线程）。
6.  `[ContentPanel.cpp:recalculateAndEmitStats]`：数据过滤改变引起模型数据变化通知。如果产生数据变更，向线程池投递任务重新计算标签和文件属性引用分布（异步，主线程 -> 线程池）。
7.  `[FilterPanel.cpp:onDirectoryStatsReady]`：计算完成后，通过 `QMetaObject::invokeMethod` 跨线程通知主线程更新高级过滤器的数量角标（异步通信，线程池 -> 主线程）。

### 4.3 主内容视图中编辑单条记录元数据（例如：修改星级评分）
1.  `[ContentPanel.cpp:GridItemDelegate::editorEvent]`：用户在卡片外的评分星星区域点击（同步，主线程）。
2.  `[ContentPanel.cpp:FerrexVirtualDbModel::setData]`：主线程捕获事件，对指定单元格索引调用 `setData(index, value, RatingRole)`（同步，主线程）。
3.  `[MetadataManager.cpp:setRating]`：模型内部调用元数据管理器的同步修改方法（同步，主线程）。
4.  `[MetadataManager.cpp:setRating]`：主线程获取 `std::unique_lock<std::shared_mutex>`，瞬间修改 `m_cache` 中该路径对应的内存镜像，然后调用 `persistAsync()` 触发落盘（同步，主线程）。
5.  `[MetadataManager.cpp:persistAsync]`：主线程直接根据修改后的元数据属性，prepare 对应的 `INSERT OR REPLACE INTO metadata ...` 语句，直接并在主线程执行 `sqlite3_step` 阻塞式写入物理磁盘库文件（同步，主线程）。
6.  `[UndoManager.cpp:pushCommand]`：向撤销栈压入 `MetadataCommand` 撤销指令（同步，主线程）。
7.  `[FerrexVirtualDbModel::dataChanged]`：发射数据改变信号，强制该卡片单元格重绘（同步，主线程）。

### 4.4 批量导入 / 物理迁移（ActionAddToCategory 动作）
1.  `[ContentPanel.cpp:onCustomContextMenuRequested]`：用户在库外项目右键，悬停“迁移”并点击目标文件夹（同步，主线程）。
2.  `[ImportHelper.cpp:importPaths]`：触发迁移方法，开始执行物理转移（同步，主线程）。
3.  `[ImportHelper.cpp:importPaths]` -> `[ShellHelper::copyOrMoveItems]`：后台工作线程执行物理文件移动（异步，主线程 -> 线程池）。
4.  `[MetadataManager.cpp:removeMetadataSync]`：后台调用方法同步移除原位置的元数据镜像（同步，工作线程）。
5.  `[AutoImportManager.cpp:onFileCreated]`：在目标位置物理移动触发 NativeFolderWatcher (IOCP) 的文件创建信号，该信号传导至 `AutoImportManager`，调用 `MetadataManager::registerItem` 将移动后的新文件同步注册入库并分配 128-bit 指纹（同步，监控工作线程）。
6.  `[ContentPanel.cpp:refreshAll]`：迁移完成后工作线程触发主线程回调，安全调用 `ContentPanel::refreshAll()` 重新加载当前导航路径（异步通信，工作线程 -> 主线程）。

### 4.5 排序列表
1.  `[ContentPanel.cpp:onCustomContextMenuRequested]`：用户在主视图右键，选择“排序”子菜单中的属性（例如按修改时间、按文件大小、或升降序）（同步，主线程）。
2.  `[ContentPanel.cpp:onCustomContextMenuRequested]`：更新 `m_sortType` 状态，并将其同步写入 `AppConfig` 配置（同步，主线程）。
3.  `[ContentPanel.cpp:onCustomContextMenuRequested]` -> `[FilterProxyModel::sort]`：调用 `m_proxyModel->invalidate()` 废弃旧索引，然后调用 `m_proxyModel->sort(0, order)` 强制重新进行排序计算（同步，主线程）。
4.  `[ContentPanel.cpp:FilterProxyModel::lessThan]`：Qt 排序算法框架在高频排序循环中，同步调用代理模型的 `lessThan` 对比前后两行（同步，主线程）。
    *   *对比细节*：根据 `m_sortType` 属性分支。若是按名称，则调用 `QFileInfo(leftRec.path).fileName()`。
5.  视图执行首帧像素重绘（同步，主线程）。

### 4.6 本地搜索列表
1.  `[MainWindow.cpp:onSearchTextChanged]`：用户在标题栏搜索框内键入文本（同步，主线程）。
2.  `[ContentPanel.cpp:search]`：搜索框的信号传递至 `ContentPanel` 的 `search(query)` 函数中（同步，主线程）。
3.  `[ContentPanel.cpp:search]`：在 `search` 内部，更新 `m_currentFilter.keyword = query`，然后调用 `applyFilters()`（同步，主线程）。
4.  `[ContentPanel.cpp:applyFilters]` -> `[FilterProxyModel::updateFilter]`：调用 `beginFilterChange` 和 `endFilterChange` 强制重新过滤（同步，主线程）。
5.  `[FilterProxyModel::filterAcceptsRow]`：对模型中的每一行，判断文件名是否包含该搜索关键词。若是则显示，否则隐藏（同步，主线程）。
6.  视图重绘搜索结果（同步，主线程）。

### 4.7 关闭窗口
1.  `[MainWindow.cpp:closeEvent]`：用户点击关闭按钮（同步，主线程）。
2.  `[MainWindow.cpp:closeEvent]`：若设置了托盘常驻，则调用 `this->hide()` 仅隐藏主界面而不退出进程（同步，主线程）。
3.  如果用户点击托盘“退出”或正常退出进程：
    *   `[DatabaseManager.cpp:~DatabaseManager]`：析构 `DatabaseManager` 单例（同步，主线程）。
    *   `[DatabaseManager.cpp:stopWorkerThread]`：向队列注入停止标志，调用 `m_queueCv.notify_all()` 并同步调用 `m_workerThread.join()` 阻塞等待同步写入线程退出（同步，主线程）。
    *   `[DatabaseManager.cpp:flushAll]`：同步向 `global.db` 及每个分库磁盘写入最新的缓存对账指标（同步，主线程）。
    *   `[DatabaseManager.cpp:closeDb]`：调用 `sqlite3_close_v2` 关闭磁盘数据库的全部活跃句柄（同步，主线程）。
4.  `main()` 函数返回，系统释放 Windows 互斥锁句柄 `ArcMeta_SingleInstance_Mutex`，进程结束（同步，主线程）。

---

## 五、界面刷新方式清单

本项目中负责数据显示与刷新重绘的核心控件刷新方式如下：

### 5.1 侧边栏用户与系统分类树
*   **绑定控件**：`CategoryPanel` 内部的 `m_categoryTree`（继承自 `QTreeView`）
*   **绑定数据模型**：`CategoryFilterProxyModel` -> `CategoryModel`（继承自 `QStandardItemModel`）
*   **数据刷新方式**：**异步全量重建**。
    *   在加载分类、分类改变或启动时，调用 `CategoryModel::refresh()`（`src/ui/CategoryModel.cpp`）。
    *   内部首先通过 `beginResetModel()` 和 `removeRows(0, rowCount())` **清空整个模型**。
    *   重新从磁盘配置和 `CategoryRepo::getAll()` 中依次重新追加所有系统行与我的分类节点行，最后 `endResetModel()`。
*   **调用频率与防抖控制**：
    *   在大量物理入库或文件变动期间，为了防止频繁执行 `refresh()` 全量重置导致分类树闪烁或假死，引入了 `QTimer m_refreshTimer` 防抖机制。
    *   每次外部请求刷新分类树时调用 `CategoryPanel::requestRefresh()`，内部调用 `m_refreshTimer->start(500)`。如果在 500ms 内再次收到变动，则重新计时。这有效地将信号风暴合并为单次刷新。

### 5.2 核心内容网格与列表展示区
*   **绑定控件**：`ContentPanel` 内的 `m_gridView`（网格视图）及 `m_treeView`（列表视图，继承自 `QTreeView`）
*   **绑定数据模型**：`FilterProxyModel` -> `FerrexVirtualDbModel`（继承自 `QAbstractTableModel`）
*   **数据刷新方式**：**分段虚拟化全量重建 + 局部增量刷新**。
    *   **全量重建（加载新文件夹/加载分类项时）**：调用 `FerrexVirtualDbModel::setRecords`。首先执行 `beginResetModel()`，接着用传入的 `std::vector<ItemRecord>` 完整覆盖底层的 `m_allRecords` 数组，最后 `endResetModel()`。由于 `FerrexVirtualDbModel` 是虚拟化表格模型，它不需要创建成千上万个 Qt 节点对象，而是由 View 滚动条滚动时动态从 `data()` 查询所需角色的数据，因此加载极快。
    *   **局部增量刷新（单项元数据改变时）**：当单个文件的星级、颜色、或标签修改时，不调用 `setRecords`。而是通过 `FerrexVirtualDbModel::updateRecordMetadata(path)` 根据物理路径在 `m_pathToIndex` 倒排索引中直接定位行索引，修改 `m_allRecords[i]` 对应的局部字段，然后发射 `emit dataChanged(left, right)` 局部更新信号，仅强制该行进行像素重绘，不破坏视图状态与滚动条位置。
*   **调用频率与防抖控制**：
    *   加载新目录和分类通过 `m_loadRequestId` 锁控制。如果前序异步扫描尚未完成，又启动了新的加载，旧结果将被直接拦截并废弃，防止列表乱跳。

---

## 六、疑似风险点标注

基于本次代码走查过程中观察到的具体代码事实，以下列出本仓库确实存在的潜在性能、死锁和线程阻塞风险。本节仅如实陈述现状：

### 6.1 `ContentPanel.cpp` 中的 `GridItemDelegate::editorEvent` 直接阻塞主线程进行数据库同步写入
*   **具体代码 facts**：
    *   `src/ui/ContentPanel.cpp` 第 1275-1314 行。
    *   该函数在主线程（UI 线程）被同步调用。当用户在卡片外的评分星星区域点击时，内部直接调用了 `model->setData(index, newValue, RatingRole)`。该调用在主线程同步触发了 `MetadataManager::instance().setRating(...)` -> `persistAsync(...)`。
    *   在 `persistAsync(...)` 内部，通过 `DatabaseManager::instance().getMemoryDb(...)` 获取磁盘数据库句柄后，直接在主线程同步执行了 `sqlite3_prepare_v2(...)`、`sqlite3_step(...)` 以及 `sqlite3_finalize(...)` DML 磁盘写入操作（Plan-130 磁盘直连句柄）。
    *   此过程在主线程中执行，未将 DML 任务投递到 `DatabaseManager` 的异步同步工作线程 `m_workerThread` 或线程池，存在因磁盘繁忙直接导致主界面发生瞬时卡死的性能隐患。

### 6.2 `CategoryModel.cpp` 里的 `CategoryModel::setData` 同步执行物理重命名与数据库更新
*   **具体代码 facts**：
    *   `src/ui/CategoryModel.cpp` 第 225-265 行。
    *   当用户对分类节点重命名时，该函数在主线程同步触发。
    *   内部如果检测到该分类关联了物理文件夹（我的分类下的 Library 子项），直接在主线程同步调用了 `QFile::rename(oldPath, newPath)`。
    *   重命名成功后，又在主线程同步调用了 `CategoryRepo::update(cat)`，后者在主线程内同步 prepare 并 step 写入全局 SQLite 表结构。
    *   当该分类对应的物理文件夹位于读取缓慢的网络驱动器、网络路径或传统机械硬盘中时，主线程中阻塞的 `QFile::rename` 和 `sqlite3_step` 将不可避免地导致主界面进入假死状态。

### 6.3 `CategoryRepo.cpp` 中的 `forEachCachedItem` 回调中调用带写锁的 API 会引发自死锁
*   **具体代码 facts**：
    *   `src/meta/CategoryRepo.cpp` 第 845-855 行。
    *   在账本对账 `fullRecount()` 函数中，代码调用了 `MetadataManager::instance().forEachCachedItem(...)`。该函数在执行期间同步获取并持有了 `std::shared_lock<std::shared_mutex> m_mutex`。
    *   在其传入的回调 lambda 内部，如果检测到分类状态与内存不一致，试图在当前线程中同步调用 `MetadataManager::instance().setManaged(path, true, false)`。
    *   在 `setManaged(...)` 的入口处，代码由于需要写入缓存，试图去获取写锁 `std::unique_lock<std::shared_mutex> lock(m_mutex)`。
    *   此时，当前执行线程已持有该互斥锁的读锁，再次在同一线程内同步请求排他的写锁将直接导致**自重入死锁**，使得整个进程无响应挂起。

### 6.4 `UiHelper.h` 中的文件图标提取同步阻塞主线程
*   **具体代码 facts**：
    *   `src/ui/UiHelper.h` 第 165-200 行。
    *   在列表视图滚动刷新时，主线程 `FerrexVirtualDbModel::data()` 中的 `Qt::DecorationRole` 同步调用了 `UiHelper::getFileIcon(path)`。
    *   在 `getFileIcon` 内部，若该后缀的图标未在缓存中命中，则会直接在主线程同步通过 `QFileIconProvider` 及系统的 Shell API 提取对应的物理文件图标。
    *   对于大文件夹中的数千个项目（尤其是非图形文件），在主线程快速滚动时高频同步查询 Shell 图标会产生极高频率的磁盘寻道 I/O，严重拖慢主线程绘制首帧，导致界面发生极为严重的视觉掉帧和卡顿。

### 6.5 `ContentPanel.cpp` 里的 `FilterProxyModel::lessThan` 在排序时动态实例化 `QFileInfo`
*   **具体代码 facts**：
    *   `src/ui/ContentPanel.cpp` 内部的 `FilterProxyModel::lessThan` 比较函数中。
    *   当进行文件名排序（`ContentPanel::SortByName`）时，在高频调用的 `lessThan` 函数内部，直接在主线程中同步对左右两项路径构造了 `QFileInfo(leftRec.path).fileName()`。
    *   由于 `QFileInfo` 构造涉及到堆内存分配及系统路径的分隔解析，在拥有数万个项目的列表进行重新排序时，构造数十万次 `QFileInfo` 会产生严重的 CPU 负载瓶颈，直接导致排序点击反应极慢。
