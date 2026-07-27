# 全款应用 SRP（单一职责原则）违背缺陷排查与极致重构规划 —— Modification_Plan-116.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本分析方案承接自 `Development_Plan.md` 中关于【继续扩大范围排查整款应用哪些存在职责过载的一律标记出来，所有不符合 SRP 规范的均需要做整改】的要求（对应用户原话：“继续扩大范围排查整款应用哪些存在职责过载的一律标记出来，必须符合‘SRP’，凡是不符合‘SRP’的都需要做整改。”）。单一职责原则（Single Responsibility Principle, SRP）强调“每个类、模块应该仅有一个引起它变化的原因”。通过对整款应用底层控制、元数据持久化、分类控制、主控制器的地毯式静态审计，我们排查出了四处严重的职责过载重灾区，并给出了极致的解耦重构规划。

## 2. 问题定位

通过对 `src/` 下的核心系统单例和控制逻辑开展深度地毯式排查，我们定位到以下严重不符合 SRP 的核心架构债务：

### 缺陷一：`MetadataManager` 承担过载——内存镜像层混杂了物理磁盘 I/O 获取、多级目录进度计算、以及高频 UI 定时刷新逻辑
- **不合理表现**：
  1. `MetadataManager` 本应是一个干净、高吞吐量的**“内存元数据高速镜像缓存（Memory Cache Mirror）”**。
  2. 但代码中，它直接调用了 Win32 原生 API `fetchWinApiMetadataDirect` 去磁盘读取文件的物理 ID、FRN 和基本时间戳，使得它开始直接涉足**“物理磁盘读取与 I/O 驱动”**。
  3. 它还直接计算并维护了文件夹百分比进度，调用 `calculateAndPersistProgress` 与 `getProgressFromDb`，硬编码处理大批量子项百分比计算，涉足了**“重型业务流程计算”**。
  4. 同时它还内置了 `m_uiSignalTimer` 定时器来处理 UI 的攒批刷新，涉足了**“UI 呈现状态流控”**。
- **危害**：内存镜像由于混杂了磁盘慢速 I/O、复杂的递归计算和 UI 定时器，引发极严重的线程竞争。一旦多工作线程触发大事务写入，读写锁就会长期阻塞在 Win32 I/O 和计算上，导致全程序假死。

### 缺陷二：`AutoImportManager` 承担过载——既当 USN 监听的分流派发者，又承担了级联物理扫描、1:1 分类树自动创建与对账逻辑
- **不合理表现**：
  1. `AutoImportManager` 理应仅作为**“自动入库流（Ingestion Broker）的派发器”**——只接受变动信号并丢进队列，维持队列顺序。
  2. 然而，在 `handleRecursiveIngestion` 中，它自己用 Qt 的 `QDir` 等去磁盘中做级联物理深度递归扫描，涉足了**“磁盘同步器（Disk Synchronizer）”**的职责。
  3. 它还直接包揽了将物理文件夹对账映射为侧边栏 1:1 动态虚拟分类树的维护动作，严重越权涉足了**“分类树状态控制器（Category Tree Controller）”**。
- **危害**：这种极度不优雅的多职责重叠，使得 USN 日志接收的极速管道极易因物理文件夹大批量重型扫描而阻塞，且使文件系统的监控流直接对数据库表结构分类产生了强编译级双向耦合。

### 缺陷三：`DatabaseManager` 承担过载——既管理 SQLite 物理连接池，又硬编码拼接了多维业务 SQL 事务
- **不合理表现**：
  1. `DatabaseManager` 本应是极纯净的**“数据库连接生命周期管理器与事务底座（DB Connection Custodian）”**。
  2. 但其代码中不仅管理了 `sqlite3*` 连接和锁，还硬编码嵌入了例如分类关联项 `category_items` 清理、标签生命周期合并等一堆具体业务的 SQL 拼接逻辑。
- **危害**：数据库驱动直接耦合具体业务表 schema，一旦业务有变，底层驱动就必须跟随编译，丧失了通用的 SQL 底座可移植性与可维护性。

### 缺陷四：`MainWindow` 承担过载——既管理顶层多容器 UI 布局，又承担了底盘级监控点火、硬件物理插入判定与注册机制
- **不合理表现**：
  1. `MainWindow` 作为顶层 UI 窗体，理应只负责**“全局多视图布局、拆分条控制及用户输入流分发（Main UI Presenter）”**。
  2. 但在其 `setupSplitters` 和初始化时，它竟然通过 `QDir::drives()` 强行在 UI 初始化线程中对所有托管库、自定义 monitored folder 发起 `addWatch` 和 `removeWatch`，成了底座级监控的**“核心点火引擎”**。
  3. 它还通过重写原生 Windows 消息过滤器，去解析 `WM_DEVICECHANGE`（`DBT_DEVICEARRIVAL` / `DBT_DEVICEREMOVECOMPLETE`）来判定物理拔插。
- **危害**：这本该是底层无头（Headless）服务或 `CoreController` 的工作。一旦主窗口初始化和渲染被卡，底层所有的 IOCP、MFT、USN 监控及硬件事件全都无法点火启动；反之，若磁盘拔插遇到高密集变动，也会将主线程直接拉死，造成恶性的 GUI 假死崩溃。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 继续扩大范围排查整款应用哪些存在职责过载的一律标记出来，必须符合“SRP”（对应用户原话：“继续扩大范围排查整款应用哪些存在职责过载的一律标记出来，必须符合‘SRP’”） | 针对 MetadataManager、AutoImportManager、DatabaseManager、MainWindow 开展地毯式审计，精细标出职责冲突与过载。 | ✅ |
| 2    | 凡是不符合“SRP”的都需要做整改（对应用户原话：“凡是不符合‘SRP’的都需要做整改。”） | 为上述四大缺陷分别设计了极致优雅的模块化解耦整改方案（第一至第四步），全面实施 SRP 整改。 | ✅ |

---

## 4. 详细解决方案

为了实现极致的 SRP 职责单一，我们将上述臃肿的单例拆解并重新编排为高度内聚、职责单一的模块矩阵：

### 第一步：对 `MetadataManager` 实施纯净高速镜像化拆分 (SRP 拆解)
1. **建立物理数据提取提取者（`PhysicalDataExtractor`）**：
   - 将 Win32 原生获取 `fetchWinApiMetadataDirect` 彻底从 `MetadataManager` 移出，专设为独立的、静态/纯函数的 I/O 模块。
2. **建立自愈进度对账服务（`IngestionProgressEngine`）**：
   - 物理隔离 `calculateAndPersistProgress` 与 `getProgressFromDb` 的计算逻辑，将进度、百分比以及递归计算逻辑移至独立的进度引擎。
3. **保持 `MetadataManager` 的纯净**：
   - `MetadataManager` 仅保留 `m_cache` 读写锁的高速存取、极轻量级的 map 倒排索引、以及对 `DatabaseManager` 的基本异步存盘持久化分发职责，退化为**纯内存元数据高速缓存层**。

### 第二步：对 `AutoImportManager` 实施事件分流与扫描剥离 (SRP 拆解)
1. **建立后台磁盘扫描对账管理器（`DiskIngestionService`）**：
   - 将物理深度递归扫描（`QDir`、`QFileInfo` 递归等）从 `AutoImportManager` 中完全移出，全部移至 `DiskIngestionService`，且限制只在后台非 GUI 的多工作线程或线程池中异步流式处理。
2. **建立分类动态绑定机（`CategoryBindingService`）**：
   - 将 1:1 分类树物理更新和树的逻辑对账逻辑剥离到 `CategoryBindingService` 中，使其专门通过调用 `CategoryRepo` 操作数据库，解除自动导入流对分类表结构的硬编码耦合。
3. **保持 `AutoImportManager` 的内聚性**：
   - `AutoImportManager` 只作为**“入库高速公路的信号调度器与缓冲区队列”**，接收通知包，压栈入库，并控制整体开始/停止。

### 第三步：对 `DatabaseManager` 实施底座化剥离与 DAO 模式解耦 (SRP 拆解)
1. **建立实体数据持久化映射器（DAO 模式，`MetadataDao` / `CategoryDao`）**：
   - 将具体的具体业务 SQL 拼接逻辑（如 `category_items` 清洗、标签更新等）从 `DatabaseManager` 中彻底剔除。
   - 新增专职处理元数据表物理读写的 `MetadataDao` 类，以及专职处理分类表的 `CategoryDao` 类。
2. **保持 `DatabaseManager` 的纯净**：
   - `DatabaseManager` 仅管理 SQLite 物理连接初始化（一盘一库）、事务的 begin/commit/rollback 纯底层抽象、连接池开闭，作为**纯数据库物理通道与事务载体**。

### 第四步：对 `MainWindow` 实施 UI 无头化拆分 (SRP 拆解)
1. **建立整机底盘硬件与监控点火器（`SystemBootstrapper`）**：
   - 将主窗口中的 `QDir::drives()` 检索托管库、对 `NativeFolderWatcher` 增删监控（`addWatch` / `removeWatch`）的重型逻辑移出。
   - 重构在 `CoreController` 或新引入的 `SystemBootstrapper` 类中。
2. **建立硬件热拔插感应驱动（`UsbArrivalWatcher`）**：
   - 在后台安装或重构专门的 Windows 消息泵窗口或利用 `SystemBootstrapper` 捕获 `WM_DEVICECHANGE` 原始消息并转换分发为通用 C++ 信号，彻底将 I/O 锁屏蔽出 GUI 呈现层。
3. **保持 `MainWindow` 的纯净**：
   - `MainWindow` 仅负责**“纯视觉布局、快捷键捕获并转发至控制器、用户手势反馈”**。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/core/NativeFolderWatcher.h` （仅限分析，不作物理改动）
- [ ] 模块/文件：`src/core/NativeFolderWatcher.cpp` （仅限分析，不作物理改动）
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` （仅限分析，不作物理改动）
- [ ] 模块/文件：`src/core/AutoImportManager.cpp` （仅限分析，不作物理改动）

**明确禁止越界修改的范围：**
- [ ] 物理 MFT 读取模块 `src/core/MftReader.cpp` —— 不修改
- [ ] SQLite 底层驱动及持久化核心逻辑 `src/meta/DatabaseManager.cpp` —— 不修改
- [ ] UI 渲染及视图交互面板 `src/ui/ContentPanel.cpp` —— 不修改

---

## 6. 实现准则与预警【核心】

1. **保持向前二进制兼容**：拆分出的 DAO 类与 Service 类对业务接口公开，原先在其他地方对 `MetadataManager::instance()` 或 `DatabaseManager::instance()` 的直接调用应在接口内安全代理，以确保整款应用其他功能模块不受影响。
2. **多线程锁层序防死锁**：当拆离 `MetadataManager` 时，高速读写锁在与底层 I/O、进度引擎交互时，必须严格遵守“先物理提取、后内存赋值”的时序，严禁在持有 `MetadataManager` 互斥锁时执行重型磁盘阻塞型 I/O，以绝死锁。
3. **QObject 生命周期自销毁**：新加入的后台服务（如 `SystemBootstrapper`）生命周期应与 `QCoreApplication` 的退出清理（`aboutToQuit`）进行精确绑定，防止产生僵尸线程。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| **侧边栏分类模式** | 所有具备“作用域”的功能（包括统计、监控反馈），其执行范围必须与 UI 顶部的 Focus Line 实时对齐，对托管文件夹及库进行精确过滤和响应。 | ✅ 符合。本方案将物理库扫描、分类树更新从核心监控层彻底解耦，并重新划归于业务层，完美保证了 Focus Line 对侧边栏和磁盘模式的对齐兼容性。 |
| **异步 IO 监控与防抖** | 采用高内聚以路径为 Key、以定时器关联的去重延迟合并，大幅削减短时间内向主线程投递高密集 `invokeMethod` 信号的风暴，杜绝 FIFO 错配。 | ✅ 符合。本方案将高密集对账计算和 UI 分流彻底划分出内存缓存层，防止 I/O 阻塞。 |

---

## 8. 待确认事项（可选）
- **无**。
