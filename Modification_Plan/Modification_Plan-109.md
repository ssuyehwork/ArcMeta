# 创建自动导入中途取消与数据擦除机制安全落地 —— Modification_Plan-107.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 NTFS 托管库自动导入（Auto-Import）进行过程中，由于导入可能涉及数万甚至数十万级的大规模文件，耗时较长。如果用户突然改变主意，当前系统缺乏一个优雅、安全的取消（Cancel）机制。单纯强杀或者放任后台不管都会带来不良后果（放任后台会导致脏数据残留与无谓的 I/O 消耗；强杀线程可能导致数据库死锁、损坏或内存泄漏）。本方案旨在设计并安全落地一套对自动导入中途取消并彻底擦除（Kill & Purge）已生成元数据和缓存垃圾的撤销机制。

## 2. 问题定位
- **后台流水线阻塞风险**：多媒体高级特征提取主要由 `MediaExtractorPipeline` 的 `processNextBatch` 及 `processRetryQueue` 异步执行。当投递任务数过大时，积压在 `m_queue` 和 `m_visualRetryQueue` 中的任务会在后台不断提取，且无法在中途被拦截或丢弃。需要支持非强杀的安全平滑取消。
- **数据残留风险**：导入过程中已有大量文件被登记写入数据库（`metadata` 及 `category_items`）。取消导入时，需要快速定位当前导入批次的宿主路径，级联清除所有对应入库的元数据和映射分类。
- **缓存垃圾残留风险**：已被提取的文件已经在内存或磁盘上生成了对应的缩略图和宽高比缓存（如 `ContentPanel` 中 `FerrexVirtualDbModel` 等视图用到的内存缓存以及磁盘图标缓存），需要对已被擦除的路径彻底物理清退，防止占用无谓的空间。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在不破坏程序稳定性的前提下，设计并实现一套中途停止和擦除机制。 (我的理解) | 4.1 节平滑退出与清除：设计 `MediaExtractorPipeline` 的原子取消标记与队列清退，确保稳定。 | ✅ |
| 2    | 用户触发取消时，首先安全、平滑地中止后台的多媒体特征提取与入库流水线 (我的理解) | 4.1 节在 `MediaExtractorPipeline` 中实现 `cancelBatch` 和 `cancelAll` 并重写事件。 | ✅ |
| 3    | 批量高效地删除该托管文件夹已入库的所有元数据记录 (我的理解) | 4.2 节在 `MetadataManager` 中增加对该文件夹下级联路径物理擦除的 `removeMetadataBatchSync` 事务清退。 | ✅ |
| 4    | 彻底物理清理已生成的对应缩略图及宽高比高级缓存，恢复到干净的状态 (我的理解) | 4.3 节对缓存管理器的缓存条目实施同步物理清除，擦除所有相关的宽高比及图标。 | ✅ |

## 4. 详细解决方案

### 4.1 安全平滑地中止后台的多媒体特征提取与入库流水线 (`MediaExtractorPipeline`)
- 在 `MediaExtractorPipeline.h` 中添加原子布尔取消标记 `std::atomic<bool> m_isCanceled{false}` 以及 `m_queueMutex` 线程保护下的清空和中止接口：
  - `void cancelAll();`：立即挂起并丢弃当前尚未处理的所有积压任务，清空 `m_queue` 与 `m_visualRetryQueue`。
  - `void cancelBatch(const std::vector<std::wstring>& paths);`：选择性丢弃指定路径或指定前缀路径下的提取任务。
- 重构 `processNextBatch()` 以及 `processRetryQueue()` 中的并发计算块：
  - 在异步循环 `for (const auto& path : batch)` 及 `processItemDirect` 内部每个可能耗时的子过程（例如 `extractColor`、`extractDimensions`）开始前，高频、实时检查取消状态 `m_isCanceled` 或当前路径是否属于已被取消的范围。
  - 一旦检测到属于被取消任务，立即跳过该项目并将其安全丢弃，不执行多媒体解析与属性设置，实现优雅的中断与平滑清场。

### 4.2 批量高效物理擦除已入库的元数据记录 (`MetadataManager` 与 `CategoryRepo`)
- 扩展 `MetadataManager` 的物理大事务擦除接口 `removeMetadataBatchSync(const QStringList& paths)`：
  - 允许传入目标托管文件夹或已被取消导入的文件夹路径。
  - 根据该根目录，级联扫描内存缓存 `m_cache` 中所有属于该前缀的路径，批量收集其对应文件的唯一 `fileId128` (即 FRN 指纹标识)。
  - 在目标物理分库（`DatabaseManager::instance().getMemoryDb`）上执行 `SqlTransaction` 高能大事务。
  - 极速批量执行 `DELETE FROM metadata WHERE file_id = ?`，并同步清退关联映射表 `DELETE FROM category_items WHERE file_id = ?`。
  - 级联清除 1:1 自动建立的整个镜像分类树节点（`CategoryRepo::remove`）。
- 偏差对账与计数器原子回置：
  - 根据删除文件的类型和个数，精准、同频地更新侧边栏原子计数器 `CategoryRepo::s_totalCount`、`CategoryRepo::s_untaggedCount`、`CategoryRepo::s_uncategorizedCount` 等。
  - 同步向 `system_stats` 写入计数增量纠正偏差，重置该文件夹在 `system_stats` 里的导入进度记录 `PROGRESS:<folder_path>`（直接在数据库中 DELETE 或归零）。

### 4.3 物理清理内存及磁盘的高级缓存与缩略图垃圾 (`CacheManager` / `FerrexVirtualDbModel`)
- 在缓存层或内容视图数据模型中（根据考古调查：通常为 `FerrexVirtualDbModel` 内的 `m_iconCache`、`m_aspectRatios` 或系统通用的 `CacheManager`）：
  - 实现无损缓存清除接口 `void clearCacheForFolder(const std::wstring& folderPath)`。
  - 遍历缓存库，凡是 Key (路径) 以前缀 `folderPath` 开头的条目，物理调用 `remove` 或 `erase`，彻底擦除其宽高比、内存缩略图和色板缓存，避免产生幽灵缓存及内存残留。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/MediaExtractorPipeline.h` / `src/meta/MediaExtractorPipeline.cpp`
  - 新增原子变量 `m_isCanceled`。
  - 实现安全中止、丢弃任务的控制接口 `cancelAll()` 及耗时循环内的原子条件拦截。
- [ ] 模块/文件：`src/meta/MetadataManager.h` / `src/meta/MetadataManager.cpp`
  - 增强 `removeMetadataBatchSync` 级联擦除的容错与事务安全性。
  - 擦除进度数据，删除 `system_stats` 里的 `PROGRESS:` 配置项并物理同步。
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` (或对应视图 Model / 缓存类，待具体执行时考古定位其物理缓存)
  - 实现对已被擦除文件夹对应缓存（图标、尺寸宽高比等）的物理清除接口。

**明确禁止越界修改的范围：**
- [ ] `MftReader` 核心 MFT 底层流扫描 —— 不修改
- [ ] `NativeFolderWatcher` NTFS 实时 USN 监控底座 —— 不修改
- [ ] 磁盘上的用户物理源文件及源文件夹 —— 明确禁止物理删除

## 6. 实现准则与预警【核心】
1. **依赖头文件与编译安全**：修改 `MediaExtractorPipeline.h` 时必须包含 `<atomic>`，确保原子状态支持。
2. **重入与死锁防护**：由于进度计算与元数据擦除涉及数据库读写，执行大事务擦除时必须按顺序获取 `DatabaseManager` 的卷排他递归锁 `getDriveMutex`，并包裹在 `SqlTransaction` 中，防止多线程竞争引发 `SQLITE_BUSY`。
3. **安全中止而非强杀**：严禁调用任何强杀线程（如 `QThread::terminate`）或毁坏流水线生命周期的操作。通过逻辑层条件（`if (m_isCanceled)`）使工作线程平滑退出，保护运行期内存分配。
4. **开箱即用与上下文契合**：方案必须保证清除后立即通过 `notifyUI(RefreshLevel::FullRebuild)` 发射全局重绘信号，让前端界面（侧边栏、内容面板）同频刷新增减，彻底抹去导入痕迹。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 数据擦除    | 数据擦除必须包含事务保证，且不得物理删除用户的磁盘物理源文件。 | ✅ 本方案严格采用 SQLite 大事务批量擦除，且声明严禁触碰用户源文件。 |
| 后台控制    | 异步后台多任务管理应支持安全中止和非强杀挂起。 | ✅ 本方案采用原子布尔标志位加耗时循环检测，确保安全平滑退出。 |

## 8. 待确认事项（可选）
- 在自动导入进行时，前端 UI 界面通常会在状态栏显示悬浮进度条。在用户触发“取消自动导入”后，是否需要在状态栏进度条及左下角文字中予以特殊视觉反馈（例如状态栏显示“已取消导入”并延迟 2 秒隐藏，同时将悬浮进度条彻底物理隐去）？此点由用户后续决定，若有需求可在实施阶段的 QWidget 交互中实现。
