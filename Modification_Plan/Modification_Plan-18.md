# 全量核心组件职责单一与高度解耦架构设计方案 —— Modification_Plan-18.md

## 1. 任务背景
在百万真实元数据规模的工业级高并发场景下，系统的模块化和解耦程度直接决定了高吞吐写入时的并发安全、性能稳定性和后期可维护性。然而，根据对现有架构的全面审计（对应用户原话：“看看“ArchitectureComplianceAudit.md”里还有哪些职责不单一且未被修改的，需要继续完成职责单一的规划”），项目中仍存在多个耦合严重、职责模糊的核心组件（FAIL 项）。这导致“底层 I/O 与上层 UI 穿透、SQL 裸写与业务逻辑混杂、文件系统操作与数据库管理倒置”等重度反模式依然残留在系统中。本方案旨在针对这些尚未重构的组件提供一份系统、完备、不留死角的职责单一化与高度解耦的架构整改路线图，并对历史遗留锁问题及生命周期进行彻底溯源与消解。

## 2. 问题定位与历史溯源

### 2.1 遗留历史问题彻底澄清

#### (a) `MetadataManager::forEachCachedItem` 自死锁问题修复验证
在先前 Plan-15 实施中，系统已在主线程/异步线程中全面消除由 `forEachCachedItem` 回调引起的自死锁隐患：
1. **统计路径绕过**：侧边栏 `getSystemCounts` 统计通过 `Modification_Plan-17.md` 引入的“内存原子寄存器”实现 $O(1)$ 读取，彻底不再通过 `forEachCachedItem` 遍历内存缓存。
2. **写动作完全剥离**：在仅存的物理校验等回调中，重构为了“两步走”防重入设计：
   - *第一步*（读锁周期）：在 `forEachCachedItem` 内部纯读取信息，将需要检测的路径追加到局部容器 `itemsToCheck`（不调用任何会申请写锁的 `setInvalid` 接口）。
   - *第二步*（锁释放周期）：`forEachCachedItem` 运行结束，其拥有的 shared 读锁已被释放。随后在循环中遍历 `itemsToCheck`，安全地调用 `MetadataManager::instance().setInvalid`（从而安全地、不产生重叠地获取 write 写锁），彻底杜绝了因“读锁未释放即尝试升级为写锁”引起的自死锁。

#### (b) Plan-16 双锁获取函数、调用路径走查与执行现状
Plan-16 已经实际执行完成。代码库中建立的双锁（`m_globalDbMutex` 全局分类库锁 与 `m_driveDbMutexMap` 分盘符隔离锁）其真实的调用路径完全符合“先全局，后分盘”的单向加锁顺序规约：
- **涉及双锁的调用路径走查**：
  - **唯一涉及双锁的函数**：`AutoImportManager::handleRecursiveIngestion` (位于 `src/core/AutoImportManager.cpp` 217~221 行)。
  - **加锁路径顺序**：
    1. 首先获取全局锁：`std::lock_guard<std::mutex> globalLock(DatabaseManager::instance().getGlobalMutex());` (保护全局分类表 categories 写入)
    2. 其次获取分盘符锁：`std::lock_guard<std::mutex> dLock(*DatabaseManager::instance().getDriveMutex(vol));` (保护对应分区库 metadata 写入)
- **并发写线程（如 USN 队列提取）**：
  - `AutoImportManager::processImportQueue` 仅针对单盘符执行物理元数据增量登记，调用路径上仅需要 `getDriveMutex(vol)` (分盘符锁) 保护，不触及全局库操作，因此只获取单把盘符锁。
- **无环锁判定结论**：全系统中仅存在这一条（先全局、后分盘）的双锁获取链，不存在任何“先盘符锁、后全局锁”的反向调用路径，加锁图（Lock Graph）天然无环，从根本上确保了高并发扫描时的死锁风险为零。

### 2.2 待解决的 7 个职责耦合 FAIL 项
根据现状审计，系统目前仍存在以下 **7 处核心职责不单一** 的耦合隐患：
1.  **`DatabaseManager`**：管理数据库连接与锁的同时，强行设置 Win32 文件隐藏属性，且在 `getMemoryDb` 内部深度嵌入了针对“盘符漂移”的物理文件重命名和冗余无效数据库的检测、移动和删除逻辑。
2.  **`MetadataManager`**：身兼“元数据提取器（Windows FID、解析图片宽高）”、“内存缓存（`m_cache` 的层级快速索引）”以及“数据访问层（SQL DML 写入）”等多重职责。
3.  **`UiHelper`**：作为 UI 样式辅助类，却混合了 `QPainter` 渲染、临时文件生成、COM 接口物理提取系统缩略图、CIE76 算法计算、以及 QFuture 异步任务调度与静态共享缓存。
4.  **`AutoImportManager`**：在 USN/MFT 监控中枢职责外，还深度干预了 1:1 逻辑分类构建（写 categories 库）与 14 条活跃路径记忆功能。
5.  **`CoreController`**：全局生命周期控制器，却在 `performSearch` 内部使用 `QDirIterator` 执行重型物理 I/O 递归扫描。
6.  **`ThumbnailDelegate` & `TreeItemDelegate`**：纯 View 渲染绘制代理，但在 `editorEvent` 中直接穿透 Model 写入星级与颜色标记，混淆了 MVC 边界。
7.  **`CategoryModel` / `CategoryPanel`**：`CategoryModel` 的 data 逻辑直接强转私有项 `CategoryItem` 并调用 `MetadataManager` 锁判定，存在细节泄露；`CategoryPanel` 视图直接耦合拖拽时的 USN 物理迁移规则判定。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 看看“ArchitectureComplianceAudit.md”里还有哪些职责不单一且未被修改的 | 对审计报告中剩余 of 7 个 FAIL 项进行全面重构规划，并解决历史自死锁、锁顺序疑问 | ✅ 一致 |
| 2    | 需要继续完成职责单一的规划 | 提供清晰的子类/接口拆分方案，隔离 UI 绘制、持久层、物理 I/O 和核心算法 | ✅ 一致 |

## 4. 详细解决方案

### 4.1 `DatabaseManager` 的连接管理与物理文件纠偏解耦
- **辅助类提取 (`DbFileSystemHelper`)**：
  新建独立的 `DbFileSystemHelper` 静态工具类，专门承接文件属性设置、盘符漂移物理重命名及冗余 DB 清除：
  ```cpp
  class DbFileSystemHelper {
  public:
      static void ensureFileHidden(const std::wstring& path);
      static QString handleDriveDriftRename(const std::wstring& volumeSerial, const QString& driveLetter, const QString& currentPath);
      static void cleanupInvalidDatabases(const std::wstring& volumeSerial);
  };
  ```
- **`DatabaseManager` 精简**：
  剔除所有 `ensureHidden` 等物理文件系统 API 调用。`DatabaseManager` 专职负责“多驱动器 SQLite 连接句柄管理、WAL模式、无阻塞微分片增量备份、多线程读写锁、后台任务队列”。

### 4.2 `MetadataManager` 拆分为缓存层与仓储/提取层
- **元数据物理提取器 (`FileMetadataExtractor`)**：
  剥离所有对 Windows API 和图像编解码库的依赖，独立负责物理特征码和多媒体信息的异步提取：
  ```cpp
  class FileMetadataExtractor {
  public:
      static std::wstring fetchFileId(const std::wstring& path);
      static bool extractDimensions(const std::wstring& path, int& width, int& height);
      static QColor analyzeDominantColor(const std::wstring& path);
  };
  ```
- **数据访问仓储 (`MetadataRepository`)**：
  将所有直接编写和执行 SQL 语句的底层数据库操作逻辑封装到专门的元数据持久化仓库中，提供强类型数据实体接口：
  ```cpp
  class MetadataRepository {
  public:
      static bool saveMeta(sqlite3* db, const RuntimeMeta& meta);
      static RuntimeMeta getMeta(sqlite3* db, const std::wstring& fileId);
      static bool deleteMeta(sqlite3* db, const std::wstring& fileId);
  };
  ```
- **`MetadataManager` 重定位**：
  重新定位为**“高性能并发内存缓存（`m_cache` + 层级快速索引）”的协调单例**。不再涉及具体的 SQL 拼接与物理磁盘解析细节，凡涉及 I/O 执行时分发给 `FileMetadataExtractor` 处理，凡涉及持久化落盘时分发给 `MetadataRepository` 提交至落盘队列，自身完全专注于高速内存检索和读写锁管理。

### 4.3 `UiHelper` 全能类的四分拆解与共享状态生命周期管理

#### 1. 拆分模块定义
- **`StylePainter`**：
  专职处理纯 Qt GUI 界面渲染逻辑，包括 QStyle设置、圆角按钮绘制辅助、主题 HSL 颜色推导。
- **`PaletteAnalyzer`**：
  封装纯粹的 CIE76 色差匹配算法、显著性调色板聚类核心数学计算。
- **`AsyncJobScheduler`**：
  封装 QFuture 与 QThreadPool 混合机制，作为通用高吞吐异步任务调度引擎。
- **`ShellIconManager` (接管物理图标/缩略图与共享状态)**：
  专职负责 Win32 Shell COM（`IShellItemImageFactory`）的多媒体缩略图提取、系统内置文件大图标获取逻辑，并全面收拢原 `UiHelper` 里的所有高风险静态共享状态。

#### 2. 共享状态生命周期精细化管理
原 `UiHelper` 中懒加载产生的 static 局部变量 `s_fileIconCache`（图标缓存）、`s_loadingKeys`（并发加载拦截集合）及通知器 `IconLoadNotifier`，在重构后将被升级并完全由 `ShellIconManager` 单例的生命周期统一管理：
```cpp
class ShellIconManager : public QObject {
    Q_OBJECT
private:
    ShellIconManager();
    ~ShellIconManager();

    QMap<QString, QIcon> m_fileIconCache;          // 升级为显式类私有成员变量，不再使用 static 局部延迟加载
    QSet<QString> m_loadingKeys;                  // 升级为显式类私有成员变量
    std::shared_mutex m_cacheLock;                // 引入读写共享锁（shared_mutex），读缓存并行化，写缓存互斥
    std::mutex m_loadingLock;                     // 保护正在加载的 key 集合

public:
    static ShellIconManager& instance();
    QIcon getFileIcon(const QString& filePath, int size = 18);
    void clearCache();                            // 暴露显式的缓存清理接口，用于内存敏感或应用退出时的生命周期安全释放
signals:
    void iconLoaded();                            // 将原 IconLoadNotifier 彻底合并，作为 ShellIconManager 的标准 QObject 信号分发
};
```
- **效果**：
  这彻底消除了 static 局部变量析构顺序不可控导致的程序退出闪退风险，且通过 `clearCache()` 实现了完美的内存释放掌控权。

### 4.4 `AutoImportManager` 捕获流程与应用历史、结构映射解耦
- **分级映射器 (`CategoryStructureMapper`)**：
  将 USN 变更中“在数据库 `categories` 中建立 1:1 树状分类目录节点”的持久化细节剥离。
- **路径历史记忆器 (`HistoryPathManager`)**：
  剥离上限 14 条的历史活跃文件夹管理，下沉至独立的导航历史记录器，专职负责访问历史的管理、去重与配置文件写入。
- **`AutoImportManager` 纯粹化**：
  专注于“监控范围过滤判定（`isUnderManagedLibrary`）、USN 捕获事件源解析派发、防丢审计触发失效标记”这 3 项核心级联与监控中枢职责。

### 4.5 `CoreController` 的系统生命周期与物理检索分离
- **物理搜索引擎 (`DiskSearchEngine`)**：
  将底层通过 `QDirIterator` 执行的物理目录迭代、深度优先扫描及文件过滤逻辑完全抽离到独立的子线程：
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
  仅承担“系统启动就绪编排、系统级通知广播（硬件热插拔）、全寿命周期状态机”的管理职责。物理搜索通过触发并监听 `DiskSearchEngine` 汇报，实现 I/O 引擎与主业务流程的高度解耦。

### 4.6 Delegate 视图项交互事件的 M-V 单向驱动重构
- **MVC 边界守护规约**：
  彻底废除 `ThumbnailDelegate` 和 `TreeItemDelegate` 的 `editorEvent` 内部直接调用 `#include "MetadataManager.h"` 对 `MetadataManager` 进行直接状态更新的穿透调用。
- **重构后交互动作驱动流**：
  1. *View 触发*：用户在 UI 界面点击星星（评分）或标签颜色。
  2. *Delegate 拦截*：`ThumbnailDelegate::editorEvent` 捕获该物理坐标点击，计算出用户期望的评星值（如 4 颗星）。
  3. *信使传递*：Delegate **不进行任何底层数据库操作，亦不修改内存缓存**，仅仅调用 `model->setData(index, 4, Roles::EditRatingRole)` 或者是发射代表 UI 请求的标准信号 `requestUpdateRating(index, 4)`。
  4. *Model/Controller 落地*：由 Model（`FerrexVirtualDbModel`）的 `setData` 函数捕获到 `EditRatingRole` 角色更新请求：
     - 调用底层的 `MetadataManager::instance().setRating` 进行内存哈希更新与异步落盘。
     - 发射标准的 `dataChanged(index, index, {Roles::RatingRole})` 信号。
  5. *View 刷新*：View 层（内容容器）接收到 Model 层的 `dataChanged` 变更通知，自动触发视口重绘，Delegate 仅在 `paint` 周期中呈现最新的评星数据。

### 4.7 视图、模型类型安全及物理规则解耦
- **`CategoryModel` 精细解耦**：
  严禁在 Model 中将 `QModelIndex` 强转为私有内部节点进行高风险操作。暴露清晰的接口返回节点的逻辑属性（如分类 ID、是否锁闭、是否加密等）。
- **物理迁移中枢 (`FileMigrationHandler`)**：
  在 `CategoryPanel` 之外独立封装物理拖拽迁移、覆盖策略、移动动作的规则检查器（`FileMigrationHandler`）。UI 只负责收集拖拽的物理路径，随后抛给该 Handler 触发 MFT/USN 并行物理迁移与事务提交。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/DatabaseManager.h` (第 80-175 行)
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp` (第 50-600 行)
- [ ] 模块/文件：`src/meta/MetadataManager.h` (第 50-345 行)
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` (第 120-2400 行)
- [ ] 模块/文件：`src/ui/UiHelper.h` (第 30-450 行)
- [ ] 模块/文件：`src/core/AutoImportManager.h` (第 20-150 行)
- [ ] 模块/文件：`src/core/AutoImportManager.cpp` (第 30-300 行)
- [ ] 模块/文件：`src/core/CoreController.h` (第 10-100 行)
- [ ] 模块/文件：`src/core/CoreController.cpp` (第 50-250 行)
- [ ] 模块/文件：`src/ui/ThumbnailDelegate.cpp` (第 20-300 行)
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h` (第 10-150 行)
- [ ] 模块/文件：`src/ui/CategoryModel.cpp` (第 20-200 行)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (第 40-500 行)

**明确禁止越界修改的范围：**
- [ ] 加密模块 `src/crypto/EncryptionManager.h/.cpp`——不修改
- [ ] 双击打开/QuickLook/图像浏览 `src/ui/QuickLookWindow.h/.cpp`——不修改
- [ ] USN底层流捕获 `src/mft/UsnWatcher.h/.cpp` 与 `MftReader.h/.cpp`——不修改

## 6. 实现准则与重构预警【核心】

### 6.1 多线程锁秩序规则（对齐 Plan-16）
- **核心澄清**：重构后的 `MetadataRepository` 在执行底层的 metadata 数据持久化时，它**仅仅触及各驱动器对应的分库（例如 D 盘对应的分库）**，因此只需要获取分盘符锁：
  ```cpp
  auto driveLock = DatabaseManager::instance().getDriveMutex(vol);
  std::lock_guard<std::mutex> dLock(*driveLock);
  ```
  它**不需要也不应该获取 `m_globalDbMutex` 全局分类库锁**。
- **锁秩序规约**：
  - 如果业务层级在某些复杂链路中（例如在 `AutoImportManager` 中执行级联入库）需要同时调用全局分类写入和对应盘符的元数据持久化，必须无条件遵循先拿 `getGlobalMutex()` (全局锁) 后拿 `getDriveMutex(vol)` (盘符锁) 的严格单向锁获取顺序，以防并发死锁。

### 6.2 Win32 COM 初始化预警
新拆分出的 `ShellIconManager` 在后台异步线程池执行 Shell 图标和缩略图提取时，由于 COM 接口的多线程敏感性，提取线程被拉起后必须先调用 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` 预热，提取完成后调用 `CoUninitialize()` 释放，否则会导致特定 Windows 环境下 Shell 提取静默失败。

## 7. 分批执行计划、依赖关系与验证方式

按照 Master Roadmap 的铁律，不接受一次性大混合提交。我们对 7 个解耦重构组件进行严格的依赖关系梳理，并划分为 **4 个渐进式批次（Phases）**。每批次重构完成后需通过对应验证方法验证通过，方可推进下一批次：

### 【第一批次】叶子节点工具类与核心数据库物理层隔离 (Phased Utilities & DB)
- **目标组件**：`UiHelper` (拆分为 `StylePainter`, `PaletteAnalyzer`, `AsyncJobScheduler` 以及 `ShellIconManager`) 与 `DatabaseManager` (提取 `DbFileSystemHelper` 物理文件属性设置与重命名)。
- **依赖关系**：无，属于 dependency tree 最底层的叶子节点。
- **整改后验证方式**：
  1. **编译检查**：确保所有依赖 `UiHelper` 的 UI 文件均编译通过，且无任何 static 析构冲突。
  2. **物理文件系统验证**：清空数据库后启动应用，检查 `.arcmeta/` 目录下数据库的生成，将盘符插入拔出模拟盘符漂移，验证 `DbFileSystemHelper::handleDriveDriftRename` 正确执行且无报错。
  3. **UI 渲染及缓存验证**：测试文件图标和缩略图的异步加载速度、缓存命中。调用 `clearCache` 接口，确认内存中 QIcon 句柄能被安全释放。

### 【第二批次】元数据核心与存储分层重构 (Phased Metadata Core)
- **目标组件**：`MetadataManager` (拆分出 `FileMetadataExtractor` 与 `MetadataRepository`)。
- **依赖关系**：依赖第一批次的 `DbFileSystemHelper`、`AsyncJobScheduler` 与 `DatabaseManager` 的纯净化连接池。
- **整改后验证方式**：
  1. **元数据读写验证**：测试在内容容器中修改任意项目的评分、修改颜色、添加和删除标签，确认底层数据变更能够通过 `MetadataRepository` 生成精确 SQL，无 `SQLITE_BUSY` 报错。
  2. **层级哈希检索验证**：快速导航任意目录，测试目录深度切换，检验 `getChildrenFromCache` 内存快速检索树（$O(1)$）的响应 and 准确性。

### 【第三批次】控制器级别业务流与物理扫描解耦 (Phased Controller & Search)
- **目标组件**：`AutoImportManager` (拆分出 `CategoryStructureMapper`、`HistoryPathManager`) 与 `CoreController` (拆分出 `DiskSearchEngine` 子线程)。
- **依赖关系**：依赖第二批次的 `MetadataRepository` 与 `FileMetadataExtractor`。
- **整改后验证方式**：
  1. **1:1 镜像同步对账验证**：在托管库内新建或改名文件夹，确认 USN 日志触发后，`CategoryStructureMapper` 正确映射并在 `categories` 中 1:1 构建对应分类树，侧边栏分类树瞬间无损刷新。
  2. **库外异步搜索验证**：进行库外路径的物理递归搜索，检验 `DiskSearchEngine` 子线程的工作状态，物理 I/O 检索时不锁死主界面，取消搜索时能够立即中止 QDirIterator 迭代。

### 【第四批次】MVC 边界治理与交互响应重构 (Phased MVC & Delegates)
- **目标组件**：`ThumbnailDelegate` / `TreeItemDelegate` (MVC 单向重构) 与 `CategoryModel` / `CategoryPanel` (类型安全与物理拖拽解耦)。
- **依赖关系**：依赖第二、三批次的数据层和业务层提供稳健的底层接口。
- **整改后验证方式**：
  1. **Delegate 零头文件包含审计**：打开重构后的 `ThumbnailDelegate.cpp` 与 `TreeItemDelegate.h`，执行物理走查，必须保证**无任何 `#include "MetadataManager.h"` 的头文件包含**，也无任何直接调用 `MetadataManager::instance()` 的代码痕迹。
  2. **评星交互单向链条验证**：
     - 在内容面板中点击第 4 颗星星。
     - 在 `FerrexVirtualDbModel::setData` 处的 `EditRatingRole` 分支中下入断点或添加 Trace 日志。
     - 确认断点成功命中，验证调用栈确由 View 抛出的 QAbstractItemModel 信号链传递而来，且能精准修改数据库、落盘，并由 `dataChanged` 信号驱动重绘，评星成功更新呈现，代表完美的 MVC 分层治理完毕。
