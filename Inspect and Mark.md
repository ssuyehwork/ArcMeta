# ArcMeta 架构与运行流程深度排查审计报告 (Inspect and Mark.md)

本报告针对当前整个应用的整体逻辑架构进行了全方位、深层次的排查与审计，重点针对“职责过载、不符合单一职责原则（SRP）、上下文冲突、重复造轮子、假死/卡顿/线程竞争、线程相互干扰”这几类核心架构缺陷进行了代码级和运行机制级的深度剖析与标记。

---

## 一、 职责过载与职责未足够单一（违反 SRP）排查与标记

在整款应用的当前架构中，存在多个核心组件承担了过多完全不相关的职责，严重违背了“单一职责原则 (Single Responsibility Principle)”，导致模块之间强耦合、高内聚丧失、测试与维护成本高昂。以下为重点排查并标记不符合 SRP 的核心类：

### 1. `MetadataManager`（元数据管理器）职责极度过载
- **现状剖析（代码级证据）**：
  - 它既是**内存元数据缓存中心**（维护 `m_cache` 及各种关联倒排索引，如 `m_fileNameToFids` 等）；
  - 又充当了**物理文件指纹获取器**（在 `fetchWinApiMetadataDirect` 中直接通过 Win32 API 提取文件的 128-bit File ID / FRN 以及大小、时间戳等物理属性）；
  - 还兼任**数据持久化执行器**（在 `persistAsync` 中直接拼接并执行 SQL 语句，控制 SQLite 事务）；
  - 甚至包含了**业务状态机控制**（如控制项目的解析吞吐、状态变更的攒批通知等）。
- **SRP 违规标记**：把“内存管理”、“磁盘/物理元数据提取”、“数据库持久化”及“状态调度”强塞在一个单例中。
- **潜在风险**：代码体量达到 2252 行。任何对 SQLite 表结构的调整或对物理指纹算法的改变都会直接危及内存缓存逻辑，且读写锁（`m_mutex`）竞争极其剧烈。

### 2. `ContentPanel`（内容展示面板）职责严重过载
- **现状剖析（代码级证据）**：
  - 既负责 **UI 树状/网格视图与布局的呈现与切换**（ListView、GridView、JustifiedViewMode 等）；
  - 又充当 **物理磁盘文件及递归目录扫描器**（`addItemsFromDirectory` 实现了本地深度遍历检索）；
  - 维护着 **本地递归扫描临时缓存** (`m_recursiveCache`)；
  - 承载了 **文件级业务操作**：如物理剪切/拷贝/粘贴 (`performCopy`/`performPaste`)、批量重命名触发 (`performBatchRename`)；
  - 直接在类内部**手动构建和驱动 29 种上下文右键菜单项** (`ContextAction`)；
  - 嵌入了 `ArcMetaVirtualDbModel`（处理高级拖拽序列化、缩略图/宽高比缓存与加载）和 `GridItemDelegate`（计算布局矩阵、处理重命名行内编辑器、按键拦截和 F2 编辑）。
- **SRP 违规标记**：混杂了“UI 视图控制”、“物理磁盘 I/O 扫描”、“本地扫描高速缓存”、“文件系统物理操作（剪贴、重命名）”、“右键菜单业务调度”等至少 5 种不同维度的职责。
- **潜在风险**：代码行数多达 3795 行，耦合度高得令人发指，极其脆弱。任何右键菜单业务调整或重命名快捷键拦截改变都会导致 3000 多行的巨型 UI 类被重构。

### 3. `MainWindow`（主窗体）职责失控
- **现状剖析（代码级证据）**：
  - 作为顶层主窗体，它本应只负责六栏布局的组装和基础的信号桥接连接；
  - 但它直接内置了 **Windows 无边框拉伸缩放计算及鼠标拖动状态机** (`getResizeDirection`, `updateCursorShape`, `m_resizeDir`)；
  - 维持着一套 **本地历史导航栈记录与调度** (`m_history`, `m_historyIndex`, `unifiedNavigateTo`)；
  - 直接硬编码嵌套了 **自定义业务对话框逻辑** (`CustomFolderImportDialog`) 及其物理增删逻辑；
  - 处理了 **高密级的定时器计时和预计耗时 (ETA) 计算** (`updateProgressBarGeometry`, `m_elapsedTimer`, `m_totalBatchCount`)。
- **SRP 违规标记**：强耦合了“窗体拖曳缩放器”、“局部业务对话框”、“导航历史管理器”以及“状态栏计时运算器”。
- **潜在风险**：长达 2292 行。无法在不影响主窗体渲染的情况下测试导航历史或重构无边框拖曳逻辑。

### 4. `FilterPanel`（筛选面板）职责混合
- **现状剖析（代码级证据）**：
  - 它不仅负责 **Adobe Bridge 风格的各种分组、色块 UI 绘制**，还直接维护了 **全局 `FilterState` 的所有高复杂度状态变更逻辑**；
  - 直接包含了 **用户输入字词过滤、相近色容差、滑杆事件处理**；
  - 内置了 **筛选历史落盘持久化管理** (`saveFilterHistory`, `getFilterHistory`)；
  - 还要根据当前数据源是物理磁盘还是虚拟镜像库，进行动态控制组隐藏 (`setMirrorSource`)。
- **SRP 违规标记**：混合了“视觉样式逻辑”、“全局过滤业务状态机”、“多维度滑杆控制”、“配置文件持久化读写”。
- **潜在风险**：代码行数达到 1501 行。过滤状态的任何逻辑扩展均需在复杂的 UI 刷新链路（`syncUIFromFilterState`）中手动对账。

### 5. `MftReader`（MFT读取引擎）职责过载
- **现状剖析（代码级证据）**：
  - 核心职责是高性能解析物理 NTFS 磁盘 MFT 分区与 USN Journal 增量更新；
  - 但它还接管了 **全局系统文件图标提取与 QIcon 缓存池** (`getCachedIcon`, `m_icon_cache`)，使用 Windows Shell COM 接口完成物理提取；
  - 同时自己实现和维护了一套 **异步元数据填充调度任务队列** (`m_metadata_queue`, `processMetadataQueue`)。
- **SRP 违规标记**：混淆了“NTFS 底层驱动/扫描”、“系统 UI 级图标解析”与“异步元数据任务调度”。
- **潜在风险**：图标缓冲区的死锁风险直接传导到多线程 MFT 磁盘扫描线程上，一旦系统 COM 接口在后台卡死，底层的 MFT 检索和变更通知也会一并永久挂起。

### 6. `DatabaseManager`（数据库连接管理器）职责越界
- **现状剖析（代码级证据）**：
  - 除了基础的 SQLite 连接、事务、备份管理之外；
  - 它直接调用 Windows API 对数据库文件设置隐藏属性 (`ShellHelper::ensureHidden`)；
  - 在 `getMemoryDb` 中还插手了对于“盘符飘移”物理连接重定向、冗余分库文件核对等偏向应用层的高级路由机制。
- **SRP 违规标记**：将“数据库物理连接建立”与“应用层隐藏属性操作”、“盘符重路由业务”混合在一起。
- **潜在风险**：底层网络或数据库类直接依赖高级文件/盘符业务，无法完成纯粹的单体离线测试。

### 7. `CategoryRepo`（分类仓储）混合统计业务
- **现状剖析（代码级证据）**：
  - 本应只是对 `categories` 表和 `category_items` 表的普通数据访问（DAO），但其内置了重型的**全账本物理核对审计** (`fullRecount`) 算法；
  - 控制了**高级状态转移逻辑**（如回收站彻底清空、节点搬移时修改内存缓存标志等）；
  - 包含了大量 **全局静态原子计数状态** 并高频多线程读写，使其成为前后台多核 CPU 缓存冲突风暴的核心。
- **SRP 违规标记**：将“数据存取层”与“重型业务审计核对”、“多线程并发全局计数器”绑定。

---

## 二、 运行流程上下文冲突排查与标记

### 1. 双轨制监控与自动入库流程的上下文竞争
- **问题现状**：在系统启动时，`NativeFolderWatcher`（IOCP）与 `UsnWatcher` 并行启动，同时 `AutoImportManager` 发起全量库同步和递归对账。当一个新文件在被高速拷贝入库时，可能产生多路并发注册信号。由于缺少路径级和 FRN 级的分布式排他处理锁，多个异步任务在同一时间对同一物理对象在内存和数据库连接中执行增、删、改操作。
- **潜在冲突**：产生竞态导致元数据状态错乱、数据库主键冲突或意外的数据标记丢失。

### 2. 搜索（`performSearch`）与异步写盘、缓存删除的上下文冲突
- **问题现状**：物理导航搜索在 `CoreController` 中通过异步 `QtConcurrent::run` 长期占有并遍历路径。如果中途用户在 UI 上对被扫描的文件进行重命名或擦除，写线程驱动 `MetadataManager` 将该缓存项彻底释放并移除，而正在读取路径以进行匹配的搜索工作线程将访问已失效的对象。
- **潜在冲突**：指针空悬、非法迭代器访问（尤其是批量多媒体解析并行写入时）。

---

## 三、 重复造轮子排查与标记

### 1. 物理资源指纹与 ID 生成机制的不统一
- `MetadataManager` 中并存 `generateFallbackFid`（物理 FRN 拼接）与 `generateDeterministicSha256Id`（SHA256 算法）。底层对 NTFS 和 Win32 标识多处硬编码。

### 2. 路径标准化（Normalization）的分散现场处理
- 路径标准化未封装为高内聚、不可变的 `Path` 领域对象，导致多处重复现场处理，且不同组件（`MftReader`、`AutoImportManager`、`DatabaseManager`）所采用的标准不完全对齐，极易引发字母大小写错配。

---

## 四、 假死、线程竞争、卡顿排查与标记

### 1. 读写大锁 `MetadataManager::m_mutex` 竞争引起的前台界面假死（高危）
- FTS5 trigram 模糊检索在主线程拉取元数据或搜索大目录时会获取并长期持有读锁（共享锁）。而后台的 MFT 扫描、USN 监控、解析管道在批量提交变动时需要获取排他性写锁。读写大锁在百万级数据下的粗粒度竞争会直接把 GUI 线程卡在等待读锁的排队队列中，引发界面崩溃般地假死。

---

## 五、 整改规划与重构方案 (整改总路线)

针对上述所有被标记、排查出的 SRP 违规和职责过载类，制订以下长效重构整改规划，并在 `Modification_Plan-111.md` 中进行详细方案落地：

1. **`ContentPanel` 模块化解耦**：
   - 将右键菜单的构建及 29 种 Action 分流到独立的 `ContentContextMenuManager` 或 `ActionController` 中，内容面板只负责捕获信号。
   - 将物理剪贴板及文件操作（剪切、拷贝、粘贴、重命名）分流到专职的 `FileSystemService`。
   - 将本地物理磁盘级递归扫描剥离给 `PhysicalDiskScanner`。

2. **`MainWindow` 职责剥离**：
   - 提取无边框窗体调整与拖动手势状态机，由专门的 `FramelessWindowResizer` 或 `WindowGestureHelper` 托管。
   - 将历史导航功能交由独立的 `NavigationHistoryService` 完全接管，主窗口仅作为状态栏/导航条的订阅者。
   - 将 5px 进度条与预计耗时 (ETA) 业务状态计算委托给 `MetadataSyncStatusMonitor` 或 `SyncStatusService`，主窗体仅承接 geometry 的无布局绝对定位绘制。

3. **`MftReader` 视觉图标服务解耦**：
   - 将 `getCachedIcon` 及系统 COM 图标提取池剥离到独立的 `ShellIconService`。
   - 将异步元数据任务调度队列剥离到 `MetadataQueueDispatcher` 中，使 MftReader 专精于高性能 MFT/USN 并发扫描。

4. **`FilterPanel` 状态逻辑拆分**：
   - 将复杂的 `FilterState` 及其多维去重过滤算法独立到单独的 `FilterEngine` 中，`FilterPanel` 仅负责单纯的 UI 数据绑定与变更信号。
