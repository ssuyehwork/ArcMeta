## [2026-07-31] 内存模式分类内容面板缩略图穿透显示

- 用户描述的现象/问题：在内存模式下，点击侧边栏分类时，内容面板把 `.arc` 资产包文件夹本身当作条目显示，呈现的是文件夹图标，而非素材缩略图。
- 用户期望的结果：内容面板应"穿透" `.arc` 包，读取包内的 `*_thumbnail.png` 文件作为该素材的视觉缩略图呈现给用户。
- 本次任务边界：修复 `ContentPanel.cpp` 中 `loadThumbnailsForRows`、`HasThumbnailRole` 和 `DecorationRole` 三处针对 `.arc` 路径的缩略图加载逻辑。
- 不在本次范围内的：不修改 `AssetImporter` 导入流程、不修改 MetadataManager 路径注册逻辑、不涉及磁盘模式（DiskNav）任何行为。
- 对应方案文档：Modification_Plan-15.md

## [2026-07-31] 内存模式资产解包重构与托管库计数矫正

- 用户描述的现象/问题：
  1. 内存数据库模式下，内容面板没有深度解包 `.arc` 容器，把 `00ms8ythbc000.arc` 等容器名当作卡片文件名展示，而不是呈现解包后的真正素材（如 `测试.psd` / `测试.md`）。
  2. 托管库节点 `ArcMeta.Library_G` 在侧边栏/快速访问中显示的计数为 `(0)`，与实际包含 of 2 个资产严重脱节。
  3. 未死守磁盘导航模式与内存数据库模式 100% 绝对隔离的原则。
- 用户期望的结果：
  1. 内存模式下彻底解包 `.arc` 容器，显示真实素材文件名与对应缩略图。
  2. `ArcMeta.Library_[盘符]` 托管库根分类节点后方的计数精准反映其包含的资产总数（如显示为 `2`）。
  3. 磁盘模式（DiskNav）与内存模式（UserCategory/SystemCategory）控制链与显示逻辑 100% 独立隔离。
- 本次任务边界：重构内存模式下 `.arc` 资产在数据库与 `ItemRecord` 的展示解包映射逻辑，修正托管库分类节点的统计与计算逻辑。
- 不在本次范围内的：不修改磁盘导航模式对原生磁盘目录的扫描行为，不改动磁盘物理文件路径。
- 对应方案文档：Modification_Plan-16.md

## [2026-07-31] 全局物理资产管线归一化与解包接口重构

- 用户描述的现象/问题：
  1. 资产导入散落于多处离散函数各自造轮子，导致包内文件被重复注册至内存缓存，“全部数据”计数在导入瞬间误飙升为 4。
  2. 无缩略图文件（如 `测试.md`）在渲染图标时误用外壳容器目录 `00ms8ythc3001.arc` 申请 Shell 图标，导致展示为黄色文件夹图标。
  3. 导入资产时未自动绑定盘符托管库根分类 ID，导致 `ArcMeta.Library_G` 节点计数归零。
  4. 缺少统一的收口接口，以前依靠局部缝缝补补打补丁，严重违反 SRP 与归一化。
- 用户期望的结果：
  1. 构建全应用唯一的 `AssetImporter::importAsset` 物理导入管线接口，单一粒子注册，自动绑定盘符托管库分类 ID，解决重复注册与计数归零。
  2. 构建统一的 `ItemRecord::fromAssetContainer` 内存解包接口与 `UiHelper::getAssetIcon` 图标接口，让无缩略图文件（如 `.md`）精准显示原生文件图标而非黄色文件夹。
  3. 构建 `CategoryRepo::recountAll` 统一计数接口，准确反映分类与托管库节点资产数。
  4. 磁盘导航模式保持 100% 独立，零解包原样遍历磁盘。
- 本次任务边界：重构 `AssetImporter` 统一导入接口、`IndexedEntry` 内存解包接口、`CategoryRepo` 计数计算与 `UiHelper` 图标提取接口。
- 不在本次范围内的：不改动磁盘导航模式的原生物理文件系统扫描逻辑。
- 对应方案文档：Modification_Plan-17.md

## [2026-08-01] 全局物理数据库同库同事务重构与语义统一

- 用户描述的现象/问题：
  1. SQLite 分库设计存在主库与驱动盘分库的“跨库撕裂”，导致写入丢失、线程中获取 nullptr 导致崩溃或静默失败。
  2. 物理托管资产本质均是 `.arc` 文件夹容器，然而数据库主键 and C++ 成员仍使用 `file_id`/`fileId`，语义模糊含混。
  3. 盘符托管根分类在侧边栏显示的计数定位模糊，导致“未分类”等逻辑桶统计产生偏差。
- 用户期望的结果：
  1. 每一个资产包的元数据（`metadata`） and 分类项目关联数据（`category_items`）100% 存放在它所属物理盘符的分库数据库中，彻底弃用全局主库的存储关联，消除跨库撕裂。
  2. 重构 `DatabaseManager::getDbForPath(path)`，只要传入路径，100% 保证打开并内存预热该分库，绝不返回 nullptr。
  3. 在 `AssetImporter::importSingleFile` 中，直接在同盘分库上开启唯一的 `SqlTransaction` 原子落盘。
  4. 全局语义重命名：数据库主键与外键列由 `file_id` 重命名为 `folder_id`，C++ 成员由 `fileId` / `fileId128` 统一重命名为 `folderId`。
  5. `ArcMeta.Library_G`仅作为侧边栏的物理入口，不作为用户语义分类；未人工手动归类前，其托管资产 100% 逻辑归属于“未分类”，保证数据对账 100% 契合（全部数据 = 未分类 = Library_G 仓库）。
- 本次任务边界：重构 `DatabaseManager`、`CategoryRepo`、`MetadataManager`、`AssetImporter` 等模块数据库存储路由、同盘事务以及全局语义标识符更名。
- 不在本次范围内的：不改动磁盘导航模式（DiskNav）的原生态磁盘物理文件系统扫描与缓存。
- 对应方案文档：Modification_Plan-18.md

## [2026-08-01] 磁盘模式缩略图缓存与双轨 100% 隔离重构

- 用户描述的现象/问题：
  1. WindowsShellThumbnailProvider 在 getShellThumbnail 中维护的 thumbs/ 缓存机制不合理，应当清理。
  2. 磁盘模式缩略图缺乏独立存放和隐藏的路径机制，存在与内存模式缩略图逻辑交叉的隐患。
  3. 磁盘模式下递归扫描文件时没有排除 .arcmeta 本身，会导致“缓存的缓存”递归问题。
  4. ContentPanel 及其底盘在多处（isManagedContext, onItem, performPaste, setData, ItemRecord::create 等）违反了“两种模式，100% 隔离”的核心规则，发生跨轨倒灌。
- 用户期望的结果：
  1. 彻底移去 WindowsShellThumbnailProvider 的缓存。
  2. 统一将磁盘模式缓存路径收口到 `.arcmeta/disk_thumbs/` 下，实现隐藏并覆盖所有分支。
  3. 磁盘扫描显式拦截并排除 `.arcmeta` 文件夹，避免递归扫描。
  4. 重构并彻底解耦 ContentPanel、setData、ItemRecord 的行为，让磁盘模式不读取 SQLite 也不在右键菜单或粘贴/拖拽中调用托管逻辑，重命名区分物理/逻辑。
- 本次任务边界：重构 `WindowsShellThumbnailProvider`、`MediaColorExtractor`、`ContentPanel` 与 `ItemRecord::create`，达到完美的双轨隔离与全新磁盘缓存规范。
- 不在本次范围内的：不修改 NativeFolderWatcher 物理文件监控底座。
- 对应方案文档：Modification_Plan-20.md

## [2026-08-01] ContentPanel 深度物理模块化拆分与 100% 架构断连

- 用户描述的现象/问题：
  ContentPanel 内部逻辑庞大，包含物理磁盘目录扫描与内存数据库模式两种截然不同的行为代码，它们在同一个类中混合并共享了诸如 `ArcMetaVirtualDbModel`、右键菜单和重命名等多项逻辑，难以实现物理级的编译断连阻断，依然具有强耦合的维护隐患。
- 用户期望的结果：
  1. 将原本极度复杂的 `ContentPanel` 拆分为 3 个职责高度单一的物理模块。
  2. 新增 `DiskExplorerPanel.h / .cpp`，负责纯物理磁盘导航（零 SQLite 数据库访问，彻底移除并禁止引入 `MetadataManager.h`、`CategoryRepo.h`、`AssetImporter.h`）。
  3. 新增 `CategoryLibraryPanel.h / .cpp`，负责数据库驱动的分类与快速访问托管库面板，引入上述托管头文件并处理素材解包与打包导入逻辑。
  4. 新建 `models` 子目录，并将 `ArcMetaVirtualDbModel` 与 `FilterProxyModel` 抽离成独立物理文件，实现 UI 与数据完全解耦。
  5. 重构后的 `ContentPanel` 仅作为一个极简的调度外壳，内部通过 `QStackedWidget` 实现对上述两个主面板的选择性分流挂载和动态切换调度。
- 本次任务边界：物理拆分与新增 `DiskExplorerPanel`、`CategoryLibraryPanel`、及独立的 models 头文件/源文件，重新编写外壳 `ContentPanel` 并更新构建系统，确保物理断连。
- 不在本次范围内的：不改动侧边栏与其他的 MainWindow 布局控制。
- 对应方案文档：Modification_Plan-21.md

## [2026-08-02] 全应用误导性命名问题排查

- 用户描述的现象/问题：整款应用可能存在一些语义不一致、容易误导开发者、或者混淆物理与逻辑模式的“误导性命名”（Misleading Naming）。
- 用户期望的结果：在分析师角色下，对全应用代码资产进行走查和审计，精准找出误导性类名、变量名、方法名或接口，并规划整改方案。
- 本次任务边界：进行全应用代码排查与静态分析，撰写对应的方案文档，不进行物理代码修改。
- 不在本次范围内的：不修改任何代码，不涉及任何物理重构执行。
- 对应方案文档：Modification_Plan-18.md

## [2026-08-02] 内存模式下受控资产包卡片解包名称与缩略图穿透修正

- 用户描述的现象/问题：在内存托管模式下，点击分类加载数据后，内容面板显示的卡片名称依然退化显示为 .arc 容器名（如“00msbqcswm003.arc”），且不显示缩略图，呈现为普通黄色文件夹图标。
- 用户期望的结果：在内存模式下，点击分类加载数据时，内容面板中呈现的受控资产包卡片其名称穿透显示为包内的真实素材文件名（例如“cx 抽象 - 736.eps”），缩略图也穿透并读取包内的高清缩略图（例如“cx 抽象 - 736_thumbnail.png”），实现完美的解包高亮显示。
- 本次任务边界：重构 `ItemRecord::create` 中对受控容器的穿透解包逻辑并进行属性防覆盖自愈；修改 `LibraryAssetModel` 的 `loadThumbnailsForRows` 对容器后缀及其斜杠路径的处理；更新 `LibraryAssetModel::data` 中 `HasThumbnailRole` 和 `DecorationRole` 对容器的穿透性判定，防止货不对板。
- 不在本次范围内的：不涉及磁盘模式任何行为、不修改任何物理文件系统及物理文件的解包、不改动 AssetImporter。
- 对应方案文档：Modification_Plan-22.md

## [2026-08-02] 修复内容面板选中项目无法拖拽到侧边栏分类功能

- 用户描述的现象/问题：由于前几次修改代码，原有的拖拽功能遭到破坏，导致现在在内容面板选中项目后，无法正常拖拽到侧边栏的分类项中。
- 用户期望的结果：在内容面板选中项目后，可以正常、顺畅地把它们拖拽到侧边树分类树对应节点（或分类空白处）中，触发分类关联或导入逻辑。
- 本次任务边界：排查内容面板各个视图模型（`DropListView`、`DropJustifiedView`、`DropTreeView`）中 `startDrag` 所生成并填充的 `QMimeData` 以及 `CategoryModel::dropMimeData` / `CategoryPanel` 及 `DropTreeView` 接受拖拽的行为（包括 `dragEnterEvent`、`dragMoveEvent`、`dropEvent`），并进行整改以保证完美的拖拽通路。
- 不在本次范围内的：不修改侧边栏和内容面板除拖拽交互以外的任何 UI 视觉布局，不修改数据库常规 CRUD 的核心操作，不改动导入逻辑底层。
- 对应方案文档：Modification_Plan-23.md

## [2026-08-02] 彻底根除全量物理对账逻辑以根除计数竞争故障

- 用户描述的现象/问题：系统频繁执行昂贵的多分区全量物理目录扫描（`syncPhysicalDirectoryCascade`）和数据库核对（`fullRecount`）机制，由于在系统启动未就绪和多数据库挂载临界时序下的资源踩踏，将空元数据快照计算出了 0 计数，脏写覆盖了分库 stats 统计表，导致侧边栏计数偶发归零以及重启在 10 变 5 变 0 之间震荡，属于不合理的垃圾逻辑架构。
- 用户期望的结果：彻底根除系统内所有全量物理目录树扫描同步、对账自愈、FRN 盘点清退以及系统启动、重载时触发的全量计数重新统计与脏写覆盖逻辑，完全不予保留，让系统彻底摆脱此机制的臃肿和脆弱隐患，使系统保持绝对的清澈与极速。
- 本次任务边界：彻底裁撤 `CategoryRepo::fullRecount` 及各分库脏写落盘事务、彻底注销 `DatabaseSynchronizer::syncPhysicalDirectoryCascade` 全量 DFS 扫描对账程序，移除 `AutoImportManager::syncAllManagedLibraries` / `handleRecursiveIngestion` 对整个受控库进行递归对账的高负载链路。
- 不在本次范围内的：不改动普通的增量文件交互（如正常导入或在软件内重命名文件时的关联更新），不修改磁盘导航模式对磁盘物理目录的原生扫描展示。
- 对应方案文档：Modification_Plan-24.md
