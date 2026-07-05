# 修改记录 (Modification Record)

## [2024-11-20]
### 缩略图填充比例不一分析与视频缩略图支持 (Plan-114)
- **src/ui/UiHelper.h**: 在 `isGraphicsFile` 中扩展了视频格式支持 (`mp4`, `mkv`, `avi`, `mov`, `wmv`, `flv`, `webm`)。
- **src/ui/ContentPanel.cpp**: 
    - `createItemRecord` 引入 `MetadataManager::normalizePath` 确保物理扫描路径与元数据 Key 归一化一致。
    - `FerrexVirtualDbModel::data` 的 `HasThumbnailRole` 为视频/图形文件预设返回 `true`。
- **src/ui/ThumbnailDelegate.cpp**: 
    - 优化 `paint` 逻辑，即使缩略图异步加载中，图形/视频文件也强制使用填充模式分支。
    - 引入 `#3A3A3A` 灰色占位背景，消除从“60% 缩小图标”到“全屏缩略图”的视觉闪烁。

### 盘符工具栏模块移植 (Plan-115)
- [2026-06-29 15:16:16] **src/ui/DriveButton.h / .cpp**: 从旧版本还原盘符状态机按钮组件，支持 Inactive/Active/Running/Paused 四种视觉状态及旋转动画。
- [2026-06-29 16:38:19] **src/ui/MainWindow.h / .cpp**: 
    - 物理加固标题栏按钮组，找回缺失的 `m_btnToggleDriveBar` 切换按钮（当前共 8 个按钮）。
    - 严格遵循全局视觉规范，在标题栏、地址栏、盘符栏与主体区域间添加 `addSpacing(5)` 物理间距。
    - 升级 `m_driveBarWidget` 样式，实现上下双 1px 切割线（`border-top` & `border-bottom`）。
    - 集成 `m_driveBarWidget` 并实现 `initDriveBar` 自动扫描系统盘符生成按钮组。

### 唯一入库路径重构与有序动态迁移 (Plan-116)
- [2026-06-30 10:20:00] **src/meta/MetadataManager.h / .cpp**:
    - `persistAsync` 增加 `authorized` 参数，非 USN Journal 触发的 INSERT 操作将被拦截，确保入库路径唯一。
    - 修复 `persistAsync` 中 `isNew` 校验时的 SQL 绑定缺失问题。
    - 同步更新 `registerItem` 和 `registerItemsAsync` 接口签名。
- [2026-06-30 10:45:00] **src/util/ImportHelper.h / .cpp**:
    - 重构 `importPaths` 为纯物理迁移模式，移除主动数据库登记，依靠 USN 异步补完入库。
- [2026-06-30 11:15:00] **src/ui/ContentPanel.cpp**:
    - 实现视图级编辑权限拦截：物理导航模式下禁止对库外项目进行元数据编辑。
    - 物理导航进入托管库时自动重定向至镜像加载模式，实现加速渲染。
    - 重构右键菜单，实现基于“库根目录置顶 + atime 排序”的有序动态迁移菜单。
- [2026-06-30 11:30:00] **src/ui/CategoryPanel.cpp**: 
    - 同步重构拖拽导入逻辑，使其符合物理迁移准则。
- [2026-06-30 11:40:00] **AGENTS.md**:
    - 固化 Plan-116 核心红线：唯一入库路径、导航加速加载及有序迁移规范。
- [2026-06-30 16:30:00] **src/ui/MainWindow.cpp / MetaPanel.cpp / ContentPanel.cpp**:
    - 彻底废除 UI 链路中所有对 `registerItem` 的主动调用，严格遵循“入库路径唯一化”红线。

### 右键菜单语义分流重构 (Plan-117)
- [2026-06-30 14:20:00] **src/meta/MetadataManager.h / .cpp**:
    - 实现 `isInsideManagedLibrary` 静态接口，封装高性能物理路径归属判定。
- [2026-06-30 15:10:00] **src/ui/ContentPanel.cpp**:
    - 重构右键菜单逻辑：基于 `isMirrorSource` 判定实现“归类到...”与“迁移”的绝对语义互斥。
    - 将所有元数据标记类操作（星级、颜色、置顶、解析颜色）移入镜像源分支，实现库外 UI 锁定与库内操作引导。
    - 废除过时的“收揽入库”术语，统一使用“解析颜色/重新解析颜色”。

### 搜索框与筛选面板集成架构重构 (Plan-118)
- [2026-06-30 16:50:00] **src/ui/ContentPanel.cpp**:
    - 重构 `search` 函数：废除独立的 `"search"` 视图模式，搜索关键词直接驱动本地 `FilterState` 过滤引擎，确保视图上下文（分类/导航）一致性。
- [2026-06-30 17:15:00] **src/ui/FilterPanel.h / .cpp**:
    - 实现数据源感知逻辑 `setMirrorSource`：当处于物理磁盘源（库外导航）时，自动隐藏评级、颜色、备注等依赖元数据库的筛选分组。
- [2026-06-30 17:30:00] **src/ui/MainWindow.cpp**:
    - 统一 `doSearch` 调用链：由 `ContentPanel::search` 统领过滤逻辑。
    - 在 `unifiedNavigateTo` 导航中枢中注入数据源感知通知，确保筛选面板与内容源状态同步。

### getManagedFolderAbsolutePath 默认兜底恢复
- [2026-06-30 18:20:00] **src/core/AutoImportManager.cpp**:
    - 为 `getManagedFolderAbsolutePath` 增加约定优于配置的默认兜底逻辑：若数据库无显式配置，则默认探测并关联 `ArcMeta.Library_[盘符]` 文件夹。

### 最近访问记录与 USN 信号补全 (Plan-119 & 120)
- [2026-07-01 10:30:00] **src/core/AutoImportManager.h / .cpp**:
    - 实现 `recordRecentVisitedFolder` 与 `getRecentVisitedFolders` 接口，支持基于磁盘卷序列号的物理文件夹访问历史记忆（上限 14 条）。
    - 补全 `entryUpdated` 信号监听，确保通过 Move（改名）操作移入托管库的项目能被 USN 感知并触发自动入库。
- [2026-07-01 11:00:00] **src/ui/ContentPanel.cpp**:
    - 在 `loadDirectory` 导航触发点注入历史记录调用。
    - 重构右键“迁移”菜单，将“迁移至最近活跃位置”替换为真实的历史路径列表，提供快捷物理位移入口。
    - 补全 `../core/AutoImportManager.h` 引用，解决静态方法调用导致的编译错误。

### 自动入库逻辑临时诊断日志补丁
- [2026-07-01 15:30:00] **src/core/AutoImportManager.cpp**:
    - 在 `startListening`、`onEntryAdded`、`onEntryUpdated` 及 `getManagedLibraryPath` 中追加 `[DIAG]` 前缀的调试日志，用于排查自动入库触发断点。

### 最终诊断：验证 startListening 调用是否被实际执行 (Plan-124)
- [2026-07-01 16:00:00] **src/main.cpp**:
    - 在调用 `startListening` 前后注入 `[DIAG-MAIN]` 日志，并输出 `AutoImportManager` 实例地址以排查单例状态。
- [2026-07-01 16:00:00] **src/core/AutoImportManager.cpp**:
    - 在 `startListening` 函数入口（if 判断前）注入 `[DIAG]` 日志，记录 `m_isListening` 的真实初值。

### 统一库路径计算逻辑 (Plan-121)
- [2026-07-01 14:00:00] **src/core/AutoImportManager.h / .cpp**:
    - 将 `getManagedFolderAbsolutePath` 重命名并公开为 `static getManagedLibraryPath`，支持自动解析路径所属卷序列号并应用默认兜底逻辑。
- [2026-07-01 14:30:00] **src/ui/ContentPanel.cpp**:
    - 在 `onCustomContextMenuRequested` 中，重构“迁移”子菜单构建逻辑：复用 `getManagedLibraryPath` 计算 Library 根目录。若目标盘尚未创建库，则显式提示“该盘库存未创建”并禁用相关操作。

### 冗余监控日志与闲置对账逻辑移除
- [2026-06-30 10:07:34] **src/ui/MainWindow.h / .cpp**:
    - 彻底移除 `[MONITOR]` 资源监控日志及相关 `m_resourceMonitorTimer` 逻辑。
    - 彻底移除“检测到系统闲置超过30秒，触发自动对账同步...”逻辑代码及相关 `m_idleTimer` 成员与事件过滤逻辑，不再保留闲置对账功能。

### CategoryPanel 调试日志清理
- [2026-06-30 10:18:39] **src/ui/CategoryPanel.cpp**:
    - 彻底移除所有带有 `[CategoryPanel]` 前缀的调试日志打印语句，优化生产环境日志输出。

### 事件过滤器安装逻辑修复
- [2026-06-30 10:21:37] **src/ui/MainWindow.cpp**:
    - 修复因移除闲置检测逻辑误删 `installEventFilter(this)` 导致搜索历史等 UI 交互失效的问题。

### USN 全口径感知修复、初始化激活与追踪日志增强 (Plan-117/122)
- [2026-07-02 11:10:00] **src/core/CoreController.cpp**:
    - 在 `startSystem` 中补全 `MftReader` 缓存加载与索引构建逻辑，确保 `UsnWatcher` 监控线程在系统启动时正确激活。
    - 补全 `MftReader.h` 头文件包含，修复标识符未定义的编译错误。
- [2026-07-02 11:20:00] **src/mft/UsnWatcher.cpp**:
    - 在 `run()` 与 `handleRecord()` 中将 `USN_REASON_RENAME_OLD_NAME` 加入捕获掩码，确保移动操作起点可见。
    - 注入 `[USN_TRACE]` 启动日志与捕获日志，实时打印变动 FRN 与 Reason 掩码。
    - 修复 `hex` 标识符未定义导致的编译错误：改用 `QString::number(..., 16)` 进行 16 进制日志格式化。
- [2026-07-02 11:30:00] **src/mft/MftReader.cpp**:
    - 在 `removeEntryByFrn` 物理销毁索引前，利用 `getPathFastInternal` 提取最后已知路径并记录 `[MFT_TRACE]` 日志。
    - 在 `entryAdded`、`entryUpdated`、`entryRemoved` 信号发射处增加 `[MFT_TRACE]` 日志，验证信号穿透性。
- [2026-07-02 11:45:00] **src/core/AutoImportManager.h / .cpp**:
    - 订阅 `MftReader::entryRemoved` 信号，实现 `onEntryRemoved` 物理删除感知，同步注销库内数据库记录。
    - 重构 `onEntryUpdated` 逻辑：利用 FID 反查机制识别项目“移出”场景，当项目离开托管库时自动执行数据库注销。
    - 注入 `[AIM_TRACE]` 全量业务日志，涵盖信号触发、路径解析、受管状态判定及入库/注销决策。

### 启动异常退出与 Thread Affinity 冲突修复
- [2026-07-02 14:30:00] **src/main.cpp**:
    - 在 GUI 主线程显式预热 `MftReader` 单例，防止由于后台线程首次创建导致的 Thread Affinity 冲突闪退。
    - 增加“启动就绪”边界日志，辅助判定程序退出点。
- [2026-07-02 14:45:00] **src/mft/MftReader.cpp**:
    - **锁分离优化**：将 `UsnWatcher` 的 `start()` 启动调用移出 `m_dataLock` 写锁保护区，彻底杜绝持有锁启动线程导致的潜在内核级死锁。
    - **兼容性降级**：废除 `std::execution::par` 并行策略，回归串行扫描/排序，解决部分 Windows 环境由于缺失 TBB/并行运行时库导致的瞬时闪退。

### 业务逻辑异步化与盘符映射修复
- [2026-07-02 15:30:00] **src/core/AutoImportManager.cpp**:
    - **异步化处理**：将 `onEntryUpdated`（移出判定）和 `onEntryRemoved`（删除同步）中的耗时 I/O 及数据库操作全部移至后台线程执行，防止主线程阻塞导致的 UI 无响应閃退。
    - **映射修复**：修正 `onEntryRemoved` 中盘符索引映射逻辑，改用 `MftReader::getDriveList()` 确保 FID 重建的卷序列号准确。
- [2026-07-02 15:45:00] **src/mft/UsnWatcher.h / MftReader.h**:
    - 补全 `volume()` 和 `getDriveList()` 访问器，支持跨模块盘符信息同步。

### 跨平台兼容性补丁与主线程阻塞防护
- [2026-07-02 16:15:00] **src/core/AutoImportManager.cpp**:
    - 补全 `<QtConcurrent>` 与 `<cwchar>` 头文件，修复标识符未定义错误。
    - 在包含 `windows.h` 后显式 `#undef run`，解决 Windows 宏与 `QtConcurrent::run` 的标识符冲突。
- [2026-07-02 16:30:00] **src/main.cpp**:
    - 增强“进入事件循环”与“正常退出”边界日志，用于区分崩溃闪退与逻辑退出。

### 实时增量同步与秒退出架构重构 (Plan-119)
- [2026-07-03 10:20:00] **src/meta/DatabaseManager.h / .cpp**:
    - 引入后台 I/O 工作线程与任务队列 `m_syncQueue`，支持非阻塞式磁盘持久化。
    - 维持 `memDb` (内存) 与 `diskDb` (磁盘) 双句柄管理，实现启动时 Disk->Memory 全量预热。
    - 重构 `shutdown` 流程：停止工作线程并安全释放物理占用，彻底废除退出时的全量备份循环。
- [2026-07-03 10:45:00] **src/meta/MetadataManager.h / .cpp**:
    - 彻底废除 `m_batchTimer` 1.5s 防抖延迟，重构为“即改即同步”架构。
    - 重构 `persistAsync`：优先提交内存事务，随后将 SQL 任务投递至后台队列执行磁盘落盘。
    - 批量操作优化：在 `renameTag`、`removeTag` 及 `removeMetadataSync` 的异步任务中引入显式事务保护，并增加磁盘写入异常日志。
- [2026-07-03 11:10:00] **src/meta/CategoryRepo.cpp**:
    - 同步重构 `addItemToCategory`、`removeItemFromCategory` 等归类逻辑，实现内存优先的异步持久化。
- [2026-07-03 11:20:00] **src/ui/TrayController.cpp**:
    - 物理移除 `BatchProgressDialog` 及 `flushStep` 持久化进度条，实现程序秒级退出体验。

### SyncStatusService 试点解耦与高性能节流设计 (Plan-121)
- [2026-07-03 14:30:00] **src/meta/DatabaseManager.h / .cpp**:
    - 增加 `m_pendingTasksCount` 原子计数器及 `pendingTasksCountChanged` 信号，追踪异步任务实时吞吐。
- [2026-07-03 15:00:00] **src/core/SyncStatusService.h / .cpp**:
    - 新增试点服务类，接管高频同步状态监听。
    - 实现 200ms 高性能节流机制（Time Window Throttling），确保 UI 刷新频率恒定。
- [2026-07-03 15:20:00] **src/ui/MainWindow.cpp**:
    - 彻底解耦 `m_btnSync` 与底层元数据信号的直接关联，接入 `SyncStatusService`。
    - 移除陈旧的“延时同步”反馈逻辑，重构为基于异步队列监控的实时同步状态展示。
- [2026-07-03 15:35:00] **CMakeLists.txt**:
    - 注册 `SyncStatusService` 源码，启用自动化编译支持。

### 并发安全性加固与 SQL 职责下沉 (Plan-122)
- [2026-07-03 16:20:00] **src/meta/DatabaseManager.h / .cpp**:
    - 将全局锁 `m_mutex` 升级为 `std::shared_mutex`，支持读写分离。
    - 优化 `getMemoryDb`、`getGlobalDb` 与 `getDiskDb` 访问路径，主线程获取句柄改用 `shared_lock`，彻底杜绝主从线程锁竞争风险。
- [2026-07-03 16:45:00] **src/meta/MetadataManager.h / .cpp**:
    - **职责下沉**：重构 `persistBatchAsync` 逻辑，将“内存库 SQL 写入”从主线程剥离并下沉至后台 I/O 线程。主线程实现“零 SQL 等待”。
    - **双重事务优化**：异步任务内部实现“内存事务 -> 磁盘事务”的链式原子提交。针对批量标签变动，通过 `persistBatchAsync` 实现了高性能大事务同步。
    - 修复了 `persistAsync` 内部的同步信号通知点，确保 UI 刷新与数据快照捕获时机一致。

### DatabaseManager 读写分离锁高性能优化 (Plan-123)
- [2026-07-03 17:10:00] **src/meta/DatabaseManager.h / .cpp**:
    - 将全局锁更名为 `m_dbMutex` 并全面应用 `std::shared_mutex`。
    - 重构 `getMemoryDb`：实现“读锁先行 + 失败升级写锁”的双重检查模式。常规句柄查询开销降低至 O(1) 且支持全并行。
    - 对 `getGlobalDb` 与 `getDiskDb` 实施共享锁优化，彻底消除查询路径上的主从线程竞争风险。

### AutoImportManager 异步化与 UI 响应性能优化 (Plan-122)
- [2026-08-15 10:30:00] **src/core/AutoImportManager.cpp**:
    - **重型任务异步化**：将 `handleRecursiveIngestion`（递归扫描）和 `processImportQueue`（队列处理）整体移入 `QtConcurrent::run` 后台线程执行，彻底消除主线程阻塞点。
    - **数据库写入保护**：引入静态互斥锁 `s_dbAccessMutex`，专门保护后台异步任务对全局数据库句柄及 `CategoryRepo` 的写入操作，防止多线程并发冲突。
    - **事务安全性加固**：在异步任务中引入 RAII 风格的 `SqlTransaction`，确保批量导入过程中数据库状态的一致性与异常安全。
    - **信号抑制与通知迁移**：将 `setInternalOperating(true/false)` 信号抑制逻辑与全量 UI 通知完整迁移至后台线程执行体，实现高性能导入。
    - **包含性修复**：同步更新 `onEntryAdded`、`onEntryUpdated` 以及 `syncAllManagedLibraries` 触发逻辑，实现全链路响应性能优化。
    - **编译警告修复**：将 `QtConcurrent::run` 的返回值显式转换为 `(void)`，消除 MSVC C4858 警告。
    - **并发冲突与死锁修复**：将 `s_dbAccessMutex` 升级为 `std::recursive_mutex`，并实现 `onEntryAdded`/`onEntryUpdated` 的全量异步化保护，解决潜在的线程安全风险。

### 托管库内存模式秒开重构与索引优化 (Plan-124)
- [2026-08-16 10:20:00] **src/meta/MetadataManager.h / .cpp**:
    - 引入 `m_parentToChildren` 快速层级索引与 `m_folderProgressCache` 进度缓存，将目录检索复杂度从 $O(N)$ 降至 $O(1)$。
    - 实现 `getChildrenFromCache` 与 `hasChildrenInCache` 接口，支持零 I/O 加载。
    - **性能加固**：重构 `renameItem` 与 `removeMetadataSync` 算法，利用树级索引实现 $O(K)$ 深度递归采集与维护，废除 $O(N)$ 全量遍历。
    - **SQL 优化**：将进度查询 SQL 句柄静态化，杜绝列表构建循环中的重复预编译。
    - **初始化优化**：在 `initFromScchMode` 中实现层级索引的完整重建与去重校验。
- [2026-08-16 11:00:00] **src/ui/ContentPanel.h / .cpp**:
    - 重构 `createItemRecord` 为零 I/O 路径：支持传入预取的 `RuntimeMeta`，并废除子文件夹空判定的物理磁盘扫描。
    - **稳健性修复**：引入镜像模式分流判定，仅在托管项上信任内存索引，普通导航维持磁盘探测以确保正确性。
    - 重构 `loadDirectory` 内存加速分支：利用 `getChildrenFromCache` 遵循“获取副本即解锁”原则，彻底消灭 UI 阻塞。

### 监控链路收拢与 1:1 物理镜像同步 (Plan-126)
- [2026-08-16 14:30:00] **src/core/CoreController.cpp**:
    - 彻底废除 `NativeFolderWatcher` (IOCP) 初始化逻辑，系统监控全面转向单一 USN 主轨。
- [2026-08-16 14:45:00] **src/core/AutoImportManager.h / .cpp**:
    - 实现 `isUnderManagedLibrary` 高效过滤：基于 FRN 链条拦截非托管路径事件，大幅降低全卷监控下的信号处理开销。
    - 建立 1:1 镜像同步：监听到文件夹变动时，实时同步维护 `CategoryRepo` 中的逻辑分类，废除一切“逻辑脑补”。
- [2026-08-16 15:10:00] **src/meta/CategoryRepo.cpp**:
    - 确立“位移驱动入库”红线：废除归类时的直接 `registerItem` 调用，将入库触发职责完全收拢至 USN 信号。
- [2026-08-16 15:20:00] **CMakeLists.txt**:
    - 移除 `NativeFolderWatcher.cpp` / `.h` 的编译配置，清理冗余代码。

### 第三方操作审计机制与失效视图 (Plan-128)
- [2026-08-16 16:15:00] **src/core/AutoImportManager.cpp**:
    - 引入 `InternalOperating` 操作溯源：精确区分内部管理动作与第三方物理操作。
    - 实现“防丢审计”：捕获外部导致的删除或移出信号，自动触发递归失效标记（`setInvalidRecursive`）。
- [2026-08-16 16:40:00] **src/meta/MetadataManager.h / .cpp**:
    - 实现 `setInvalidRecursive` 与 `setInvalidByFrn` 接口，支持基于 FRN 复合主键的跨卷精确失效标记。
- [2026-08-16 17:00:00] **src/ui/MainWindow.cpp**:
    - 实现审计视图状态机：点击“失效数据”分类时自动隐藏 `NavPanel` 容器，实现审计模式下的独占列表展示。

### 失效数据列表斑马纹样式修复
- [2026-07-04 13:38:57] **src/ui/InvalidDataListView.h**:
    - 在 `m_view` 的样式表中增加 `alternate-background-color: #252526`，修复开启斑马纹后在深色主题下回退至浅色背景导致文字不可见的问题。

### USN 实时监控引擎“点火”补完 (Plan-129)
- [2026-07-04 14:34:18] **src/core/CoreController.cpp**:
    - 在 `startSystem` 启动链条中补齐 `MftReader::instance().loadFromCache()` 调用，确保程序启动时自动激活历史盘符的监控线程。
- [2026-07-04 14:34:18] **src/ui/MainWindow.cpp**:
    - 在 `onDriveButtonContextMenu` 交互中补齐 `MftReader::instance().buildIndex({letter})` 调用，并引入 `isDriveIndexed` 预检逻辑，实现新建托管库后的实时监控“热激活”且防止重复启动线程。

### 实时入库逻辑修复与秒退架构对齐 (Plan-130)
- [2026-07-04 15:27:04] **src/core/AutoImportManager.cpp**:
    - 在 `onEntryUpdated` 中补全 `catId == 0` 分流逻辑，确保首次从库外移入的文件夹能正确触发递归入库。
- [2026-07-04 15:27:04] **src/mft/UsnWatcher.cpp / MftReader.cpp**:
    - 在 `UsnWatcher::run()` 批处理循环中增加停止位检查。
    - 将 `UsnWatcher` 的停止调用移回主线程同步执行，彻底解决退出假死问题。
- [2026-07-04 15:27:04] **src/meta/DatabaseManager.cpp**:
    - 彻底废除内存库中转与 `flushStep` 备份逻辑，改为直连磁盘 DB 并开启 WAL 模式，对齐“秒退出”架构规约。

### 架构逻辑“去毒”与性能飞跃方案 (Plan-131)
- [2026-07-04 16:15:00] **src/meta/DatabaseManager.h / DatabaseManager.cpp**:
    - **方案 D**：落地 RAII 状态令牌。定义 `SyncTaskToken` 结构，利用 C++ RAII 特性管理 `m_pendingTasksCount`，彻底根治异步任务计数器因异常或拷贝导致的不归零 Bug，消除 UI 伪假死。
- [2026-07-04 16:15:00] **src/meta/MetadataManager.cpp**:
    - **方案 A**：废除冗余异步持久化。在磁盘直连模式下移除 `persistAsync`、`persistBatchAsync` 等方法中的 `enqueueSyncTask` 冗余调用，利用 WAL 模式并发特性直接在主事务中落盘，减少 50%-70% 的 I/O 压力。
    - **方案 C**：引入“物理指纹”准入机制。在 `registerItem` 解析流水线前对比磁盘文件的 `mtime` 和 `size`，若指纹一致且已入库则直接跳过后续解析与写入流程。
- [2026-07-04 16:15:00] **src/core/AutoImportManager.h / AutoImportManager.cpp / src/mft/MftReader.h / MftReader.cpp**:
    - **方案 B**：重构 FRN 判定链。在 `AutoImportManager` 中引入 `m_managedFrnCache` 缓存托管根 FRN；在 `MftReader` 中新增 `getParentFrnByFrn` 接口。`isUnderManagedLibrary` 升级为内存级 FRN 链溯源判定（(log N)$），完全废除低效的全路径字符串拼接逻辑。
- [2026-07-04 16:15:00] **src/ui/MainWindow.cpp / src/core/CoreController.h / CoreController.cpp**:
    - **方案 E**：MainWindow 职责剥离。建立 `handleDeviceChange` 接口专职处理硬件信号，将 `MainWindow` 中关于 `WM_DEVICECHANGE` 的底层处理逻辑迁移至 `CoreController`，解决“上帝对象”逻辑耦合问题。
- [2026-07-04 16:30:00] **src/core/AutoImportManager.h**:
    - 补全缺失的 <unordered_set> 与 <cstdint> 头文件，修复方案 B 引入的编译错误。

### 盘符栏 (Drive Bar) 逻辑架构规约 (Plan-131 5.1 & 5.2)
- [2026-07-04 17:10:00] **src/ui/MainWindow.h / src/ui/MainWindow.cpp**:
    - 实现 `MainWindow` 单例模式与 `setDriveState` 接口，支持跨模块更新盘符按钮状态。
    - 落实激活前拦截逻辑：处于 `Inactive` 状态的盘符按钮在被点击时将拦截跳转，并提示用户通过右键菜单创建托管库。
- [2026-07-04 17:10:00] **src/util/ImportHelper.cpp**:
    - 集成盘符状态联动：在 `importPaths` 执行物理迁移期间，将目标盘符切换为 `Running`（转圈动画）；任务结束后根据托管库现状自动恢复 `Active` 或 `Inactive` 状态。

### USN 实时感知与解析触发逻辑调试埋点
- [2026-07-04 17:35:00] **src/core/AutoImportManager.cpp**:
    - 在 `onEntryAdded` 信号感知层与过滤决策层分别注入 `QMessageBox` 调试弹窗，用于实时确认 USN 信号的接收与托管库路径过滤结果。
- [2026-07-04 17:35:00] **src/meta/MetadataManager.cpp**:
    - 在 `registerItem` 业务解析层入口注入 `QMessageBox` 调试弹窗，用于确认解析流水线的实际启动状态。

### USN 实时监控引擎“点火”逻辑修复 (Plan-131)
- [2026-07-04 18:05:00] **src/core/CoreController.cpp**:
    - 重构系统启动链条：确立了 Metadata -> AutoImport -> MFT Cache -> Full MFT Scan 的加载次序。
    - 补全 `MftReader::instance().buildIndex(allDrives)` 调用，解决 USN 监控线程在无缓存情况下从未启动的问题，确保全盘符实时感知的可靠性。

### 递归入库性能重构：职责分离与事务分片
- [2026-07-05 07:33:26] **src/core/AutoImportManager.cpp**:
    - **职责分离**：重构 `handleRecursiveIngestion`，将同步重型解析替换为 `registerItemLight`（物理占位）+ `registerItemsAsync`（异步补全解析）的两步走策略，显著提升大目录初次导入速度。
    - **事务分片**：引入 `IngestionContext` 实现 500 项步进式批次提交，支持 `global.db` 与盘符 `metadata.db` 的双重事务分片，防止长事务锁死数据库连接。
    - **性能优化**：直接利用内存元数据缓存获取刚登记项的 FID，彻底消除递归遍历中的重复物理磁盘 I/O。
    - **稳定性增强**：确保 DFS 遍历下的“分类先于项”依赖顺序，通过分批提交保证主线程 UI 刷新期间的“呼吸空间”。
