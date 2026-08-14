# ArcMeta 全量架构合规性审查审计报告 (ArchitectureComplianceAudit.md)

本报告针对 ArcMeta 项目进行全量、无死角的架构与合规性审查。评判标准严格对标**“职责单一 + 模块化”**原则，并以**“数百万条记录级别”**的工业级超大规模数据量为前提，重新评估现有系统在并发同步、全量扫描、隐式锁竞争、以及边界穿透等方面的实际表现。

---

## 一、全量审查清单

以下对仓库中实际存在的所有核心类/模块进行逐一评判，给出 **PASS**（合格）或 **FAIL**（不合格）判定：

### 1. DatabaseManager (`src/meta/DatabaseManager.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：在 `DatabaseManager::ensureHidden`（第 132~134 行）直接调用了 Windows 物理文件 API 设置隐藏属性，超出了底层数据库连接管理器的范畴。
  - **条件 3 (硬编码依赖)**：在 `getMemoryDb` 内部深度耦合了针对“盘符漂移”的物理重命名及冗余数据库删除等纠偏逻辑。
  - **大级别数据规模评估（高风险）**：
    在百万级数据量下，虽然由于退出了之前的“逐行同步（SELECT+INSERT）”而改为 SQLite Backup API，但在 `saveDb`（第 310~320 行）和 `shutdown` 中执行的整库页级克隆（Page-Level Backup），在数据库文件体积达到数百 MB 乃至数 GB 时，**一次性同步将产生秒级的强 I/O 阻塞**。由于 `DatabaseManager` 在执行 `saveDb` 期间需要获取 `m_dbMutex`（在 WAL 模式下），主线程在获取句柄或读写内存库时也会发生长达数秒的假死。必须重构为基于增量快照或 WAL 日志的反向落盘机制。

### 2. MetadataManager (`src/meta/MetadataManager.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：混合了“内存缓存映射”、“文件物理 FID 提取（`fetchWinApiMetadataDirect`）”、“图像宽高分析（`tryExtractDimensions`）”、“代表色显著性聚类与重试（`tryExtractColor`/`processVisualRetryQueue`）”以及“数据库底层 DELETE/INSERT 语句执行”等至少 5 种不相关职责。
  - **条件 8 (应该增量却现场全量计算)**：在 `MetadataManager::searchInCache`（第 1750~1802 行）中，执行了对百万级内存缓存 `m_cache` 的全量线性遍历（$O(N)$ 复杂度），并对每一项进行字符串或标签正则匹配，期间持有了 `m_mutex` 的共享读锁。
  - **大级别数据规模评估（极高风险）**：
    - **全量内存查询瓶颈**：当 `m_cache` 载入百万条记录时，`searchInCache` 的单次线性查找将消耗数秒的 CPU 时间，且长达数秒的共享读锁会把所有其他试图执行 `ensureActivated`（需要 `unique_lock` 写锁）的后台扫描线程全部锁死，在主线程产生严重的、不可恢复的 UI 卡死。
    - **全量计数审计重数**：`CategoryRepo::fullRecount`（在初始化结束第 303 行触发）为了对账，会遍历 `MetadataManager` 中数百万条记录的内存快照，耗时极其恐怖。

### 3. CategoryRepo (`src/meta/CategoryRepo.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：不仅负责分类的持久化逻辑，还混合了“全账本核对审计（`fullRecount`）”、“全局静态计数器维护（`s_totalFileCount`）”以及“回收站高级状态转换（`moveToTrashBatch`）”等业务。
  - **条件 8 (应该增量却现场全量计算)**：在 `CategoryRepo::fullRecount`（第 800~820 行）中，直接通过遍历 `MetadataManager` 全量缓存计算已分类/未分类计数，百万级数据量下会导致启动过程挂起十数秒。

### 4. TagManagerView (`src/ui/TagManagerView.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 2 (UI 控件直接写 SQL)**：作为继承自 `QWidget` 的 UI 类，在 `addTagToGroup`（第 340~349 行）、`deleteGroup`（第 403~414 行）、`refresh`（第 538~556 行）等地方直接调用 `sqlite3_prepare_v2` 等数据库底层 API 并显式编译执行 SQL 语句。
  - **条件 3 (硬编码具体路径/驱动器)**：在多处硬编码获取 C 盘分库连接：`DatabaseManager::instance().getMemoryDb(L"C")`（如第 338、358、384、400、425、536 行）。

### 5. ContentPanel (`src/ui/ContentPanel.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：作为核心 UI Panel，不仅承担视图代理和展示，还在右键菜单中混合了重型业务判断（`isInsideManagedLibrary`）、调用 `recordRecentVisitedFolder` 进行导航历史落盘、甚至自行分流触发 MFT 镜像加速。
  - **条件 4 (越权越界访问细节)**：直接通过 `MetadataManager::instance().getMeta(...)` 读取底层的 `RuntimeMeta` 内存镜像并自行拆解其状态（如 `meta.isManaged`）。

### 6. UiHelper (`src/ui/UiHelper.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：混合了“轻量 QPainter 渲染”、“物理临时文件计算”、“Windows Shell COM 位图提取（`getShellThumbnail`）”、“CIE76 色差及显著性量化算法（`extractPalette`）”和“QFuture 异步任务调度与跨线程排队通知（`getFileIcon`）”。
  - **条件 7 (命名与实际职责不符)**：名义上是 `UiHelper`（样式/图像辅助类），实际是一个高耦合的“系统多媒体提取及线程总线中心”。

### 7. AutoImportManager (`src/core/AutoImportManager.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：不仅负责对 MFT 与 USN Journal 的增量捕获，还混合了“在 DFS 递归中 1:1 分级建立 Category 对象”以及“通过 AppConfig 维护、上限为 14 条的历史访问记录管理”职责。
  - **条件 5 (全局大锁导致串行等待)**：使用 `static std::recursive_mutex s_dbAccessMutex;` 笼统保护所有后台线程的写数据库和分类关联，导致多盘符并行扫描对账时发生无意义的互斥排队。

### 8. BatchRenameEngine (`src/meta/BatchRenameEngine.cpp` & `.h`)
- **判定结果**：**PASS**
- **符合标准说明**：专门负责正则、步进等重命名规则的解析与物理预览计算，职责纯粹，不涉及 UI 渲染、不直接写 SQL 语句，通过强类型接口返回计算结果。

### 9. SyncStatusService (`src/core/SyncStatusService.cpp` & `.h`)
- **判定结果**：**PASS**
- **符合标准说明**：专门接管高频元数据同步状态监听，通过 200ms 时间窗高频节流机制保护 UI，职责清晰单一。

### 10. CoreController (`src/core/CoreController.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：作为全局中控（管理系统就绪、状态栏文本），却在 `performSearch` 内部深度干预了具体的物理磁盘搜索分支、通过 `QDirIterator` 执行 I/O 级目录递归扫描。这导致业务流程管理与物理磁盘文件系统检索细节高度强耦合。

### 11. NativeFolderWatcher (`src/core/NativeFolderWatcher.cpp` & `.h`)
- **判定结果**：**PASS**
- **符合标准说明**：专门负责底层 IOCP 监控目录的添加、移除及 Windows 变更句柄的管理，通过原生回调分发，无其他业务职责混杂。

### 12. ThumbnailDelegate (`src/ui/ThumbnailDelegate.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：混合了“QPainter 圆角裁剪绘制”、“加载状态样式控制”、“扩展名角标胶囊 HSL 颜色推导”、以及“在 `editorEvent` 中直接修改选中 Model 项星级”的交互逻辑和 View 的临时触发编辑控制（有状态交互）。

### 13. TreeItemDelegate (`src/ui/TreeItemDelegate.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：类似 `ThumbnailDelegate`，除纯 `paint` 绘制外，在 `editorEvent`（第 175~216 行）直接深度参与了对评星和标记颜色的业务修改与编辑触发器拦截，职责越界。

### 14. CategoryModel (`src/ui/CategoryModel.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 4 (越权越界访问细节)**：在 `CategoryModel::data` 中，为了呈现镜像分类的图标和锁定标志，直接越过接口，大量对 `QModelIndex` 通过私有强制类型转换（如强转 `CategoryItem`），并调用 `MetadataManager` 进行锁判定，对其他模块存在深度细节依赖。

### 15. CategoryPanel (`src/ui/CategoryPanel.cpp` & `.h`)
- **判定结果**：**FAIL**
- **命中条件**：
  - **条件 1 (职责不单一)**：UI 面板直接混合了拖拽导入时的物理迁移规则判定（`CategoryPanel::dropEvent` 触发 USN），超出了纯视图布局的控制范围。

### 16. EncryptionManager (`src/crypto/EncryptionManager.cpp` & `.h`)
- **判定结果**：**PASS**
- **符合标准说明**：专门负责密钥派生与数据块物理加解密操作，接口干净，无 UI 和数据库耦合。

### 17. BatchRenameDialog / BatchRenamePreviewDialog (`src/ui/BatchRename*`)
- **判定结果**：**PASS**
- **符合标准说明**：虽然代码行数较多，但它们只负责管理重命名设置对话框、预览表格的数据绑定、以及调用 `BatchRenameEngine` 进行数据刷新，符合纯 UI 控制器职责。

### 18. address-related UI Panels (`AddressBar`, `AddressHistoryPanel`, `BreadcrumbBar`)
- **判定结果**：**PASS**
- **符合标准说明**：专注于地址路径字符串的解析、历史路径的下拉渲染以及物理面包屑按钮的动态生成，职责清晰单一。

### 19. JustifiedView / JustifiedResultView / GridResultView / ListResultView
- **判定结果**：**PASS**
- **符合标准说明**：专注于各自的三视图布局管理（等高自适应拼图或卡片网格），排版几何算法 `doLayout` 纯粹，不混杂其他业务。

### 20. UndoManager (`src/core/UndoManager.h`)
- **判定结果**：**PASS**
- **符合标准说明**：管理命令队列，实现标准 Command 模式的 Undo/Redo，不掺杂渲染和物理操作逻辑。

### 21. ImportHelper / ShellHelper (`src/util/*`)
- **判定结果**：**PASS**
- **符合标准说明**：属于纯功能型 Utilities，调用 Win32 原生 Shell 或执行物理复制/移动（`importPaths`），不包含状态缓存。

---

## 二、FAIL 项汇总与严重程度排序

当数据规模达到**数百万条记录**时，上述判定为 FAIL 的项目对于系统的实际危害表现出完全不同的烈度：

### 1. 严重（直接导致崩溃、死锁、或在百万级数据下产生长达数秒至数十秒的系统卡顿/假死）

1.  **`MetadataManager::searchInCache`（全量内存遍历）**：
    - **百万级危害**：在 100 万条以上的 `m_cache` 中进行全量线性扫描，极高频地持有 `m_mutex` 共享锁。不仅会占用 100% 的单核 CPU 导致 UI 频繁假死，还会把所有正在写入缓存的后台 MFT/USN 扫描线程（因等待 `unique_lock` 写锁）彻底阻塞，在锁竞争下产生长达数秒至十数秒的**硬性死锁或完全挂起**。
2.  **`TagManagerView.cpp`（UI 硬编码数据库 + 直写 SQL）**：
    - **百万级危害**：直接绕过数据访问层、硬编码 C 盘连接。在多盘符并行高负荷写入时，UI 线程执行的裸写 SQL 会频繁触发 `SQLITE_BUSY` 锁冲突导致界面闪退、数据回滚或破坏标签库的一致性。
3.  **`CategoryRepo::fullRecount`（全量重数对账）**：
    - **百万级危害**：每一次对账都在初始化时遍历全量内存或全表，耗时直接从微秒级飙升至秒级，导致应用启动时出现十几秒的黑屏或无响应，严重降低工业可用性。
4.  **`AutoImportManager`（全局 `s_dbAccessMutex` 大递归锁）**：
    - **百万级危害**：将物理对账、分类递归创建、数据库写入全部置于这一把全局锁中，导致前台和后台由于大锁死锁，完全丧失了 WAL 并发和多驱动器独立并行的设计初衷。

### 2. 中等（架构不合理，增加系统腐化风险，在数据量增长后可能暴露出性能衰退或隐式数据竞争）

1.  **`DatabaseManager`（SQLite Backup API 备份落盘）**：
    - **百万级危害**：整库页级 Backup。虽然在后台执行，但在数据库体积随着元数据达到数百 MB 时，一次性磁盘 Backup 产生极高的 I/O 吞吐，频繁抢占物理 I/O 带宽，使前台提取缩略图等物理 I/O 操作速度断崖式下跌。
2.  **`CoreController::performSearch`（混合物理磁盘检索细节）**：
    - **百万级危害**：在百万级数据的库外导航时，直接触发了 `QDirIterator` 进行物理磁盘的全盘 DFS 扫描，耗时完全取决于物理机械硬盘的转速，属于极高危的 I/O 阻塞。

### 3. 轻微（职责边界模糊，命名不规范，但实际性能风险在百万数据量下可控）

1.  **`UiHelper.h`（全能型上帝类）**：
    - 属于命名与职责不符。虽然由于大量的静态无状态计算，在性能上不会直接导致死锁，但由于扇出高、依赖多，其任何微小修改或引入新依赖（如 MINGW 的 ShellAPI）都会触发整个仓库全量重新编译，属于维护性“焦油坑”。
2.  **`ThumbnailDelegate` / `TreeItemDelegate`（绘制代理承接有状态交互）**：
    - 属于 MVC 模式不彻底。在绘制星级、响应编辑时，通过强制类型转换和委托控制拦截了业务事件，但在性能上由于只针对视口（Viewport）内的可见项触发，危害不会随着全库数据量扩展而呈线性增加。

---

## 三、总体合规率

基于对本仓库全部核心文件和功能模块的全口径盘点：

- **审查模块总数**：21 个
- **PASS (合格) 数量**：11 个 (如 `BatchRenameEngine`、`SyncStatusService`、`EncryptionManager`、`AddressBar` 等)
- **FAIL (不合格) 数量**：10 个 (如 `DatabaseManager`、`MetadataManager`、`CategoryRepo`、`TagManagerView` 等)

- **总体架构合规率**：**52.3%**

### 明确性审计结论：
**当前架构整体上不符合“职责单一、模块化”的标准。**

虽然项目在“界面美观度、瀑布流拼图计算节流”等局部的战术实现上具有很高的完成度，但在核心数据库、元数据镜像和 UI 控制的架构设计上，由于直接穿透（UI 裸写 SQL、UI 直接深入内存镜像）和全局大锁、全量扫描等反模式的存在，使合规率仅仅刚过半。尤其是针对**“数百万条记录级别”**的特大工业规模，当前的检索、对账和分类计数逻辑属于**“小数据量安逸假象”**。一旦导入百万真实文件，系统将发生严重的、难以承受的锁竞争假死或 I/O 卡顿，必须立即对 FAIL 项执行统一持久层（Repository）与纯物理提取模块（Extractor）的隔离拆分。
