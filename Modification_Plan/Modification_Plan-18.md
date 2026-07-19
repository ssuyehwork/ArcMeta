# 全量核心组件职责单一与高度解耦架构设计方案 —— Modification_Plan-18.md

## 1. 任务背景
在百万真实元数据规模的工业级高并发场景下，系统的模块化和解耦程度直接决定了高吞吐写入时的并发安全、性能稳定性和后期可维护性。然而，根据对现有架构的全面审计（对应用户原话：“看看“ArchitectureComplianceAudit.md”里还有哪些职责不单一且未被修改的，需要继续完成职责单一的规划”），项目中仍存在多个耦合严重、职责模糊的核心组件（FAIL 项）。这导致“底层 I/O 与上层 UI 穿透、SQL 裸写与业务逻辑混杂、文件系统操作与数据库管理倒置”等重度反模式依然残留在系统中。本方案旨在针对这些尚未重构的组件提供一份系统、完备、不留死角的职责单一化与高度解耦的架构整改路线图。

## 2. 问题定位
根据现状审计与共识，目前系统存在以下 **7 处核心职责不单一** 带来的架构隐患（对应用户原话：“看看“ArchitectureComplianceAudit.md”里还有哪些职责不单一且未被修改的，需要继续完成职责单一的规划”）：
- **定位 1（`DatabaseManager`）**：
  - **耦合现象**：在管理数据库连接与并发控制的同时，直接调用 Win32 物理文件 API（`SetFileAttributesW`）来设置隐藏属性，且在 `getMemoryDb` 内部深度嵌入了针对“盘符漂移”的物理文件重命名和冗余无效数据库的检测、移动和删除逻辑。
- **定位 2（`MetadataManager`）**：
  - **耦合现象**：身兼“元数据提取器（获取 Windows FID、解析图片宽高宽高）”、“高性能内存缓存器（`m_cache` 的哈希和层级索引）”以及“数据库底层持久化（执行增删改查 SQL）”等多重职责，是一个典型的耦合型单例。
- **定位 3（`UiHelper`）**：
  - **耦合现象**：作为名义上的 UI 样式辅助类，却深度混合了 `QPainter` 圆角渲染、临时文件路径生成、COM 接口物理提取系统缩略图（`getShellThumbnail`）、CIE76 色差及显著性量化算法（`extractPalette`）、以及 `QFuture` 多线程异步任务调度。
- **定位 4（`AutoImportManager`）**：
  - **耦合现象**：在处理 MFT/USN Journal 变更的主轨流程之外，还负责在 DFS 递归扫描中 1:1 分级建立逻辑分类节点（写 `categories` 表）、以及使用 `AppConfig` 维护、上限为 14 条的历史访问目录记录管理职责。
- **定位 5（`CoreController`）**：
  - **耦合现象**：作为全局业务流中控与生命周期管理者，其内部却在 `performSearch` 内部深度干预具体的物理磁盘搜索分支，利用 `QDirIterator` 在主/子线程中执行高阻塞的物理 I/O 级目录递归扫描，严重混淆了控制链与数据源细节。
- **定位 6（`ThumbnailDelegate` & `TreeItemDelegate`）**：
  - **耦合现象**：作为纯 View 层的绘制代理，除了执行 paint 渲染外，在 `editorEvent` 中直接深度参与了对评星和标记颜色的业务修改，调用了对 Model 底层项 of 修改接口，破坏了 MVC 的单向依赖。
- **定位 7（`CategoryModel` / `CategoryPanel`）**：
  - **耦合现象**：`CategoryModel` 的 `data` 逻辑直接强转私有项 `CategoryItem` 获取状态并越权调用 `MetadataManager` 锁判定，存在细节泄露；`CategoryPanel` 视图面板直接参与了拖拽导入时的物理迁移规则判定。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 看看“ArchitectureComplianceAudit.md”里还有哪些职责不单一且未被修改的 | 对审计报告中剩余的 7 个 FAIL 项进行全面重构规划，逐一消灭职责耦合 | ✅ 一致 |
| 2    | 需要继续完成职责单一的规划 | 提供清晰的子类/接口拆分方案，隔离 UI 绘制、持久层、物理 I/O 和核心算法 | ✅ 一致 |

## 4. 详细解决方案

### 4.1 `DatabaseManager` 的数据库连接与物理文件操作隔离
- **物理辅助类抽象 (`DbFileSystemHelper`)**：
  将文件隐藏属性、盘符漂移重命名和无效 DB 冗余清理这 3 项物理 I/O 职责剥离，新建 `DbFileSystemHelper` 静态工具类：
  ```cpp
  class DbFileSystemHelper {
  public:
      static void ensureFileHidden(const std::wstring& path);
      static QString handleDriveDriftRename(const std::wstring& volumeSerial, const QString& driveLetter, const QString& currentPath);
      static void cleanupInvalidDatabases(const std::wstring& volumeSerial);
  };
  ```
- **`DatabaseManager` 精简**：
  彻底废除 `ensureHidden` 方法。当 `getMemoryDb` 探测到需要进行重命名或无效清理时，直接委托给 `DbFileSystemHelper`。`DatabaseManager` 本身只保留“多驱动器 SQLite 句柄连接池管理、WAL 并发模式设置、无阻塞增量备份、多线程读写锁协同、后台任务队列调度”等 pure-database 职责。

### 4.2 `MetadataManager` 拆分为缓存层与仓储/提取层
- **元数据物理提取器 (`FileMetadataExtractor`)**：
  剥离所有对 Windows API 和图像编解码库的直接依赖，提取到独立功能类中，专门负责物理特征码和多媒体信息的异步提取：
  ```cpp
  class FileMetadataExtractor {
  public:
      static std::wstring fetchFileId(const std::wstring& path);
      static bool extractDimensions(const std::wstring& path, int& width, int& height);
      static QColor analyzeDominantColor(const std::wstring& path);
  };
  ```
- **数据访问仓储 (`MetadataRepository`)**：
  剥离所有直接编写和执行 SQL 语句的底层数据库操作逻辑，集中沉淀到专门的元数据持久化仓库中，提供强类型数据实体接口（例如 `RuntimeMeta`）：
  ```cpp
  class MetadataRepository {
  public:
      static bool saveMeta(sqlite3* db, const RuntimeMeta& meta);
      static RuntimeMeta getMeta(sqlite3* db, const std::wstring& fileId);
      static bool deleteMeta(sqlite3* db, const std::wstring& fileId);
      static bool setInvalidFlag(sqlite3* db, const std::wstring& fileId, bool invalid);
  };
  ```
- **`MetadataManager` 重定位**：
  重新定位为**“高性能并发内存缓存（`m_cache` + 层级快速索引）”的协调单例**。不再涉及任何具体的 SQL 拼接与物理磁盘解析细节，凡涉及 I/O 执行时异步分发给 `FileMetadataExtractor` 处理，凡涉及持久化落盘时分发给 `MetadataRepository` 提交至后台落盘队列，自身完全专注于无锁/轻量级读写锁的高速读写服务。

### 4.3 `UiHelper` 全能上帝类的四分拆解
- **拆分 A（`StylePainter`）**：
  纯粹的 QStyle、圆角按钮绘制辅助、主题 HSL 颜色推导等纯 Qt GUI 界面样式绘制逻辑。
- **拆分 B（`ShellThumbnailExtractor`）**：
  专职负责 Win32 Shell COM（`IShellItemImageFactory`）的多媒体缩略图提取、系统内置文件大图标获取逻辑。
- **拆分 C（`PaletteAnalyzer`）**：
  封装纯粹的 CIE76 色差匹配算法、显著性调色板聚类核心数学计算。
- **拆分 D（`AsyncJobScheduler`）**：
  封装 QFuture 与 QThreadPool 混合机制，作为通用高吞吐异步任务调度引擎，专门为 UI 和后台提供非阻塞队列支持。

### 4.4 `AutoImportManager` 捕获流程与应用历史、结构映射解耦
- **分级映射器 (`CategoryStructureMapper`)**：
  将 USN 日志捕获或目录扫描中“在数据库 `categories` 中建立 1:1 树状父子节点”的逻辑剥离：
  ```cpp
  class CategoryStructureMapper {
  public:
      static int ensureCategoryStructureForPath(const std::wstring& physicalPath);
  };
  ```
- **路径历史记忆器 (`HistoryPathManager`)**：
  剥离上限 14 条的历史活跃文件夹管理，将其下沉至独立的导航历史记录器，专职负责访问历史的管理、去重与配置文件写入：
  ```cpp
  class HistoryPathManager {
  public:
      static void recordVisitedFolder(const std::wstring& path);
      static std::vector<std::wstring> getRecentVisitedFolders();
  };
  ```
- **`AutoImportManager` 纯粹化**：
  专注于“监控范围过滤判定（`isUnderManagedLibrary`）、USN 捕获事件源解析派发、防丢审计触发失效标记”这 3 项核心级联与监控中枢职责。

### 4.5 `CoreController` 的系统生命周期与物理检索分离
- **物理搜索引擎 (`DiskSearchEngine`)**：
  将底层通过 `QDirIterator` 执行的物理目录迭代、深度优先扫描及文件过滤逻辑完全抽离：
  ```cpp
  class DiskSearchEngine : public QThread {
      Q_OBJECT
  public:
      void startSearch(const QString& rootPath, const QString& keyword);
  signals:
      void fileFound(const QString& path);
      void searchFinished();
  };
  ```
- **`CoreController` 纯粹化**：
  仅承担“系统启动就绪编排（Orchestration）、系统级通知广播（如硬件热插拔分发）、全寿命周期状态机”的管理职责。物理搜索通过触发并监听 `DiskSearchEngine` 汇报，实现 I/O 引擎与主业务流程的高效解耦。

### 4.6 Delegate 视图项交互事件的 M-V 单向驱动重构
- **事件单向联动设计**：
  彻底废除 `ThumbnailDelegate` 和 `TreeItemDelegate` 的 `editorEvent` 内部对 `MetadataManager` 进行直接状态更新的穿透调用。
- **重构机制**：
  - 当 Delegate 捕获到评星或标签颜色点击事件时，不进行任何业务级数据修改，仅发射代表 UI 操作请求的标准信号 `requestUpdateRating(index, rating)`。
  - 该信号连接至 `ContentPanel`（作为 Controller）或通过 Model 层的 `setData` 并配合自定义角色（如 `EditRatingRole`）执行。Delegate 只读、只绘，不决定、不修改底层业务模型数据。

### 4.7 视图、模型类型安全及物理规则解耦
- **`CategoryModel` 精细解耦**：
  严禁在 Model 中将 `QModelIndex` 强转为私有内部节点进行高风险操作。暴露清晰的接口返回节点的逻辑属性（如分类 ID、是否锁闭、是否加密等）。
- **物理迁移中枢 (`FileMigrationHandler`)**：
  在 `CategoryPanel` 之外独立封装物理拖拽迁移、覆盖策略、移动动作的规则检查器（`FileMigrationHandler`）。UI 只负责收集拖拽的物理路径，随后抛给该 Handler 触发 MFT/USN 并行物理迁移与事务提交。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：
  - `src/meta/DatabaseManager.h` / `.cpp`
  - `src/meta/MetadataManager.h` / `.cpp`
  - `src/ui/UiHelper.h`
  - `src/core/AutoImportManager.h` / `.cpp`
  - `src/core/CoreController.h` / `.cpp`
  - `src/ui/ThumbnailDelegate.cpp` / `TreeItemDelegate.h`
  - `src/ui/CategoryModel.cpp` / `CategoryPanel.cpp`

**明确禁止越界修改的范围：**
- [ ] 本 Turn 属于资深程序员·纯分析师模式。根据角色红线规约，**绝对禁止创建任何具体的物理代码文件，亦不得修改任何现有物理代码文件**。
- [ ] 方案中的所有伪代码设计和重构思想只可在本 `.md` 规划方案文档中陈述，不进行任何编译调试与环境部署。

## 6. 实现准则与预警【核心】
1. **多线程锁秩序预警**：重构后的 `MetadataRepository` 在被异步工作线程调用持久化时，必须保证先获取 `DatabaseManager` 的全局读写锁，再执行具体的 SQL 操作，严防线程安全死锁。
2. **Win32 COM 初始化预警**：新拆分出的 `ShellThumbnailExtractor` 在后台多线程中被调用时，每个调用线程必须先显式调用 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` 预热 COM 组件套间，否则将导致缩略图提取静默失败。
3. **MVC 分离一致性**：在改造 Delegates 交互事件时，必须确保不回退、不引入任何直接引用 `MetadataManager` 的操作，维持完美的“事件单向派发，数据单向流动”原则。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 纯分析师模式 | Jules 本 Turn 仅输出方案，绝不修改任何物理代码文件 | ✅ 符合，仅提供 Modification_Plan-18.md 方案文档 |
| 职责单一化规约 | 底层数据库连接、文件操作、多媒体提取、UI 绘制应实现绝对的物理/逻辑隔离 | ✅ 符合，精准设计了辅助工具类与持久层解耦接口 |
| 避开 O(N) 全量 | 统计或搜索需求应避免全表或全缓存线性遍历，通过高效哈希与数据库直接检索 | ✅ 符合，通过拆分 `MetadataRepository` 精确化了查询路径 |
