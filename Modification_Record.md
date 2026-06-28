# Modification Record

## 2026-11-14
- **任务描述**: 「空格键」快速预览深度优化与全口径属性过滤 (Plan-109)。
- **修改文件**:
    - **修改**: `src/ui/QuickLookWindow.cpp` (实现分流渲染、增加 50MB 大图保护、重构滚动条样式)
    - **修改**: `src/ui/ContentPanel.cpp` (建立全口径黑白名单防御机制)
- **修改原因**: 解决预览高清图片时的严重锯齿、滚动条样式偏离规范以及非预览项（文件夹/压缩包/二进制）被错误触发的问题。
- **优化点**:
    - **极致画质**: 针对标准图像（jpg/png/webp 等）启用全分辨率加载，避开缩略图引擎的插值损耗。
    - **性能稳健**: 引入 50MB 物理阈值，对超大图自动降级至高清缩略图，兼顾画质与 UI 响应。
    - **规范统一**: 废除硬编码样式，物理对齐 `Memories.md` 滚动条标准（10px, 3px radius）。
    - **白名单优先**: 严密拦截文件夹及一切不支持预览的后缀（exe/zip/dll 等），确保预览行为的确定性。

- **任务描述**: 缩略图加载视觉抖动治理 (Plan-108)。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.cpp` (图形文件等待缩略图时返回空图标)
    - **修改**: `src/ui/ThumbnailDelegate.cpp` (绘制灰色圆角矩形占位背景)
- **修改原因**: 消除加载图形文件时从“系统图标”到“缩略图”的二段式切换抖动。
- **优化点**:
    - **视觉一致性**: 图形文件在加载期间不再显示无关的系统图标，而是显示纯净的灰色占位背景，使缩略图出现时更自然。
    - **性能友好**: 仅针对图形文件执行占位逻辑，非图形文件维持原有图标展示逻辑。

- **任务描述**: 内容面板加载性能与视觉闪烁修复 (Plan-106)。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.cpp` (移除异步加载前的 `m_model->clear()` 调用)
- **修改原因**: 消除切换目录或分类时的“闪白/闪黑”现象，通过保留旧数据直到新数据就绪，实现平滑过渡。
- **优化点**:
    - **视觉平滑**: 移除预清空逻辑，利用 `setRecords` 的原子替换特性，将视图闪烁降至毫秒级感知。
    - **状态一致性**: 仅在确定无数据（如空路径或搜索重置）时执行同步清理，确保 UI 反馈的真实性。

- **任务描述**: 导航面板布局还原与收藏夹修复 (Plan-107)。
- **修改文件**:
    - **修改**: `src/ui/NavPanel.h/.cpp` (物理还原 `QSplitter` 架构，移除 `QScrollArea`)
- **修改原因**: 修复导航面板无法拖拽调节比例、收藏夹展示位置异常及高度锁死问题。
- **优化点**:
    - **架构回归**: 恢复 `QSplitter` 弹性布局，允许用户手动调节磁盘树与收藏夹比例。
    - **逻辑解锁**: 彻底废弃不稳定的手动像素高度计算，将空间分配交还给布局引擎。

- **任务描述**: 全局信号风暴治理与增量 UI 刷新 (Plan-106)。
- **修改文件**:
    - **修改**: `src/meta/MetadataManager.cpp` ( Setter 通知由 Rebuild 降级为 PathUpdate)
    - **修改**: `src/ui/MainWindow.h/.cpp` (集成 300ms 搜索防抖计时器)
    - **修改**: `src/ui/CategoryPanel.h/.cpp` (集成 300ms 分类过滤防抖计时器)
    - **修改**: `src/mft/MftReader.cpp` (引入批量 USN 信号整形，阈值 50 项)
    - **修改**: `src/ui/FilterPanel.h/.cpp` (实现 `syncUIFromFilterState` 增量刷新逻辑，修复 `Q_OBJECT` 缺失编译错误)
- **修改原因**: 解决高频交互导致的 UI 假死、搜索卡顿及批量入库时的信号洪流问题。
- **优化点**:
    - **按需刷新**: 元数据变更不再触发全量重建，仅重绘对应行。
    - **防抖处理**: 搜索输入引入 300ms 缓冲区，大幅降低 CPU 瞬时负载。
    - **增量渲染**: 过滤器面板实现原地状态同步，杜绝暴力销毁重建导致的视觉闪烁与内存抖动。

- **任务描述**: 内容面板交互与刷新逻辑优化 (Plan-105)。
- **修改文件**:
    - **修改**: `src/ui/DropTreeView.cpp` (移除 `dragMoveEvent` 中的 `setCurrentIndex` 调用)
    - **修改**: `src/ui/DropJustifiedView.cpp` (移除 `dragMoveEvent` 中的 `setCurrentIndex` 调用)
    - **修改**: `src/meta/MetadataManager.h/.cpp` (引入 `m_isInternalOperating` 标志位并拦截 `notify` 信号)
    - **修改**: `src/ui/ContentPanel.cpp` (在 `onPathsDropped` 期间启用信号抑制锁并手动执行单次刷新)
- **修改原因**: 解决拖拽过程中元数据面板频繁更新导致的假死、竞争问题，并修复操作完成后触发两次刷新的冗余逻辑。
- **优化点**:
    - **交互解耦**: 彻底隔离了拖拽反馈与选择信号，释放主线程。
    - **智能刷新**: 通过抑制锁机制合并了同步与异步刷新，提升了 UI 稳定性。

## 2026-10-29
- **任务描述**: 内容面板拖拽投放功能修复 (Plan-104)。
- **修改文件**:
    - **修改**: `src/ui/DropJustifiedView.h/.cpp` (补全 `pathsDropped` 信号及 `dragEnter/Move/Drop` 事件处理逻辑)
    - **修改**: `src/ui/DropListView.h/.cpp` (补全 `pathsDropped` 信号及事件处理逻辑)
    - **修改**: `src/ui/ContentPanel.h/.cpp` (启用视图 `DragDrop` 模式；连接投放信号；实现 `onPathsDropped` 槽函数处理物理移动/复制逻辑；补全缺失头文件)
- **修改原因**: 修复内容面板无法拖拽投放的问题，恢复“拖拽文件/文件夹到目标文件夹”的核心交互功能。
- **优化点**:
    - **交互增强**: 实现了拖拽过程中的目标项实时高亮反馈。
    - **逻辑闭环**: 打通了从 UI 投放信号到物理 `ShellHelper` 操作及 `UndoManager` 撤销栈的完整链路。
    - **健壮性**: 补全了 `QFileInfo`、`QDir`、`QApplication`、`UndoManager` 等依赖，修正了信号连接语法。

- **任务描述**: 托管文件夹命名规范化（卷序列号模式）与存量自动化扫描优化 (Plan-105)。
- **修改文件**:
    - **修改**: `src/ui/DriveButton.h/.cpp` (新增 `Missing` 状态及其对应的置灰视觉样式)
    - **修改**: `src/core/AutoImportManager.h/.cpp` (实现 `getManagedLibraryPath` 接口；引入路径前缀缓存 `m_activeDrivePrefixes`；重构判定与扫描逻辑以支持 `Arcmeta_[SERIAL]_[LETTER]` 规范；补全 `QtConcurrent` 依赖并加固宏防御)
    - **修改**: `src/ui/MainWindow.cpp` (全量切换至基于卷序列号的动态路径构造逻辑；集成目录探测与存量扫描触发；处理 `#undef run` 宏冲突)
    - **修改**: `src/util/ImportHelper.cpp` (引入 `#undef run` 宏防御)
- **修改原因**: 统一托管文件夹命名标准与数据库对齐（卷序列号模式），增强物理识别度，解决盘符激活后“存量文件”自动入库问题。
- **优化点**:
    - **命名规范升级**: 托管库路径由 `ArcMeta.Library_X` 升级为 `Arcmeta_[SERIAL]_[LETTER]`（如 `D:\Arcmeta_4DFFAF5E_D`），实现托管文件夹与特定物理卷的强绑定，防止盘符漂移导致的识别错误。
    - **性能加固**: 引入路径前缀缓存机制，使 USN 增量判定的时间复杂度降低至 O(1)，且通过 MFT 内存索引实现存量秒级扫描。
    - **编译稳固**: 彻底消除了 Windows 环境下 `run` 宏冲突导致的构建障碍，并将 `getEntryCount` 归一化为 `totalCount`。

- **任务描述**: 盘符激活异步化重构与判定逻辑标识符规范化 (Plan-104)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.cpp` (重构 `onDriveButtonClicked` 为异步模式，引入 `QtConcurrent::run` 后台激活物理层；添加 `<QtConcurrent>` 与 `<QSet>` 头文件；优化 `loadActiveDrives` 启动恢复逻辑；规范右键菜单 `targetPath` 声明)
    - **修改**: `src/core/AutoImportManager.cpp` (重构 `isPathInManagedLibrary` 判定函数，补全 `QString` 类型定义并统一使用 `targetPath` 标识符；同步更新 `onEntryAdded/Removed` 变量命名)
- **修改原因**: 解决点击盘符导致 UI 线程阻塞假死的问题，修复因标识符未声明、类型缺失或命名不一致导致的编译及监听失效故障。
- **优化点**:
    - **异步响应**: 磁盘扫描与数据库挂载移至后台，彻底消除 UI 线程阻塞。
    - **状态闭环**: 引入 `s_activatingDrives` 静态锁，确保初始激活期间逻辑原子性，防止重复触发。
    - **判定逻辑加固**: 规范化托管路径判定算法，统一基于 `QString` 进行高效前缀匹配。

- **任务描述**: 启动监听失效修复、变量作用域补完及冗余日志清理 (Plan-103)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h/.cpp` (补全 `m_activeDrives` 等成员变量；在 `onDriveButtonClicked` 中显式触发数据库挂载与 `MftReader::buildIndex` 以激活物理监听；统一右键菜单中的 `targetPath` 标识符及 Lambda 捕获；移除 `[MONITOR]` 资源监控日志)
    - **修改**: `src/core/AutoImportManager.cpp` (补全 `isPathInManagedLibrary` 函数中的 `QString` 类型定义及变量名统一)
    - **修改**: `src/ui/MetaPanel.cpp` (彻底移除 `ElasticEdit` 的高度调整调试日志)
- **修改原因**: 解决物理监听引擎未实际启动导致的 UI “纹丝不动”问题，并修复因标识符命名冲突、类型定义缺失导致的编译报错。
- **优化点**:
    - **驱动链闭环**: 打通了从 UI 点击到物理 MFT 引擎启动的断层，确保文件变动能即时被感知。
    - **编译稳固**: 彻底消除了“未声明标识符”、“缺少类型说明符”及“未引用局部变量”等编译阻塞与警告点。
    - **逻辑一致性**: 统一了库路径变量命名，提升了代码的可维护性与上下文一致性。

## 2026-10-28
- **任务描述**: 托管文件夹监听与入库行为归一化 (Plan-99)。
- **修改文件**:
    - **修改**: `src/ui/DriveButton.h/.cpp` (扩展 `State` 枚举，支持 Inactive/Active/Running/Paused 状态切换与 SVG 动画绘制)
    - **修改**: `src/core/AutoImportManager.h/.cpp` (重构任务控制逻辑，支持分盘监听开关、队列暂停/恢复，实现 `ArcMeta.Library` 路径精准过滤与自动出库联动)
    - **修改**: `src/ui/MainWindow.h/.cpp` (集成盘符工具栏状态机，实现点击流转、右键菜单创建/打开托管文件夹、以及激活状态的持久化记忆)
- **修改原因**: 实现托管文件夹的自动化管理，使用户能够通过盘符工具栏直观地控制 `ArcMeta.Library` 的监听状态，并与系统现有的导入/出库逻辑保持一致。
- **优化点**:
    - **状态可视化**: 通过 `refresh` (旋转) 和 `pause` 图标实时反馈后台任务进度。
    - **任务隔离**: 支持对不同物理磁盘独立设置监听与暂停状态，互不干扰。
    - **静默入库**: 自动感应 USN 变更并执行去抖批量入库，确保 I/O 效率。
    - **持久化**: 盘符激活状态记录于 `AppConfig`，应用重启后自动恢复监听。
- **紧急修复与稳定性增强**:
    - **变量命名纠正**: 彻底解决了 `AutoImportManager.cpp` 中 `qPath` 标识符未声明的问题（已规范化为 `targetPath`）。
    - **编译依赖修复**: 补全了 `MainWindow.cpp` 中缺失的 `DriveButton.h` 头文件，解决了类型未定义导致的连带错误。
    - **类型系统强化**: 在 `MainWindow.h` 中修正了 `m_driveButtonMap` 的成员类型，并移除了不必要的 `dynamic_cast`。
    - **跨平台兼容**: 将宽字符底层比较重构为 `QString` 逻辑，消除了 `_wcsnicmp` 在不同编译器下的参数匹配风险。
    - **容器标准化**: 将所有非标准库容器统一回退至 `QSet` 与 `QHash`，以符合项目既有的 Qt 开发规范。
- **细节治理与警告消除**:
    - **代码清理**: 移除了 `MainWindow.cpp` 中未使用的局部变量，消除了 MSVC 编译警告 C4189。
    - **逻辑闭环**: 补全了 `AutoImportManager` 与 `MainWindow` 之间的任务状态同步链路。

## 2026-10-27
- **任务描述**: 目录导航面板隐式双区拆分及收藏功能实现 (Plan-95)。
- **修改文件**:
    - **修改**: `src/ui/NavPanel.h/.cpp` (引入 `QSplitter` 拆分上下区，新增 `m_favoriteView` 及其持久化逻辑，统一收藏区标题栏样式与高度)
    - **修改**: `src/ui/ContentPanel.h/.cpp` (新增 `setPendingSelectName` 接口并配合 `m_isPendingEdit` 支持外部定位)
    - **修改**: `src/ui/MainWindow.cpp` (连接收藏点击信号，处理文件定位导航)
- **修改原因**: 增强导航效率，允许用户通过拖拽将常用文件/文件夹固定在导航栏下方，实现一键跳转。并根据用户反馈调整了收藏区标题栏的高度与视觉风格，使其与整体 UI 更加和谐。
- **优化点**:
    - **布局对齐**: 采用显式 Splitter 分割线（#333333）并移除顶层容器边距，确保背景色全宽显示。
    - **视觉统一**: 将收藏区标题栏高度调整为 32px，背景色对齐 `#252526`，并通过 15px 物理填充确保内容与主标题栏完美对齐。
    - **交互联动**: 收藏文件点击后自动导航至父目录并高亮定位该文件，而不进入编辑模式。
    - **持久化**: 收藏列表通过 JSON 序列化存储在 `AppConfig` 中，支持手动拖拽重排顺序。
    - **样式修正**: 移除了收藏区标题栏的 `border-top` 样式，消除其与 `QSplitter` 句柄的物理重叠，解决了分割线过粗的视觉问题。


[2026-07-18 17:30:00]
- **任务描述**: 分类面板底部集成搜索过滤功能 (Plan-97)。
- **修改文件**:
    - **修改**: `src/ui/CategoryPanel.h/.cpp` (引入 `CategoryFilterProxyModel` 代理模型，新增复刻系统标准的本地搜索栏)
    - **修改**: `src/ui/CategoryDelegate.h` (实现 `#41F2F2` 亮蓝色关键词高亮渲染)
- **修改原因**: 修复分类搜索过滤功能缺失及逻辑架构断路的问题，提升大规模分类体系下的定位效率。
- **优化点**:
    - **1:1 标准复刻**: 搜索框使用 `select` 图标，启用原生清除按钮，严格执行 6px 圆角与 `#2D2D2D` 背景规范。
    - **递归过滤引擎**: 实现基于 `NameRole` 的树形递归匹配，解决由于父节点过滤导致匹配子节点被屏蔽的“傻逼逻辑”；增加调试日志。
    - **视觉高亮**: 引入三段式绘制逻辑，精准高亮匹配词条而不受 `(n)` 计数器干扰。
    - **交互稳健性**: 全量补全代理索引映射逻辑，确保重命名、展开、点击等交互安全。

[2026-07-18 16:30:00]
- **任务描述**: 导航面板隐式双区拆分及收藏功能实现 (Plan-95)。
- **修改文件**:
    - **修改**: `src/ui/NavPanel.h/.cpp` (引入 `QSplitter` 拆分上下区，新增 `m_favoriteView`)
    - **修改**: `src/ui/ContentPanel.h/.cpp` (新增 `setPendingSelection` 接口支持外部定位)
    - **修改**: `src/ui/MainWindow.cpp` (连接收藏点击信号，处理文件定位导航)
- **修改原因**: 增强导航效率，允许用户通过拖拽将常用文件/文件夹固定在导航栏下方，实现一键跳转。
- **优化点**:
    - **布局对齐**: 采用透明 Splitter 手柄实现“隐式”拆分。
    - **交互联动**: 收藏文件点击后自动导航至父目录并高亮定位该文件。
    - **持久化**: 收藏列表通过 JSON 序列化存储在 `AppConfig` 中，支持手动拖拽重排顺序。

[2026-07-18 16:00:00]
- **任务描述**: 文件夹显隐状态与筛选勾选逻辑联动 (Plan-94)。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.cpp` (更新 `FilterProxyModel::filterAcceptsRow` 判定逻辑)
- **修改原因**: 解决顶栏“文件夹显隐”按钮关闭时，筛选面板中的“文件夹”和“空文件夹”选项失效的问题。
- **优化点**:
    - **逻辑解耦**: 引入“显式选中通过权”，使得用户即使关闭了全局文件夹显示，仍能通过右侧筛选器有针对性地调出文件夹，提升操作灵活性。

[2026-07-18 15:30:00]
- **任务描述**: “空文件夹”筛选选项置顶显示。
- **修改文件**:
    - **修改**: `src/ui/FilterPanel.cpp` (调整“空文件夹”在“文件类型”分组中的位置)
- **修改原因**: 响应用户审美习惯，将“空文件夹”复选框移动到“文件类型”分组的最顶部。
- **优化点**:
    - **UI 布局优化**: 将重要或常用的特殊筛选项置顶，提升交互效率。

[2026-07-18 15:00:00]
- **任务描述**: 恢复“空文件夹”筛选位置并修正 Lambda 捕获 Bug。
- **修改文件**:
    - **修改**: `src/ui/FilterPanel.cpp` (恢复“空文件夹”至“文件类型”，移除“属性”分组，修复悬空引用)
- **修改原因**: 
    1. 响应用户习惯，将“空文件夹”选项从临时增加的“属性”分组移回其原始的“文件类型”分组内。
    2. 根除 `rebuildDateCheckboxes` 中通过引用捕获局部变量导致的程序崩溃风险。
- **优化点**:
    - **交互对齐**: 保持 UI 布局的逻辑连续性。
    - **稳定性增强**: 将 Lambda 捕获改为按值捕获 `isCreateDate` 并动态解析目标列表，确保内存安全。

[2026-07-18 14:30:00]
- **任务描述**: 彻底消除搜索逻辑双轨制 (Plan-92 终期重构)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h/.cpp` (移除 `performSearch` 异步调用及 `m_activeSearchReqId` 成员)
- **修改原因**: 按照筛选器架构统一方案，搜索框应作为筛选器的一个输入组件，走本地 `filterAcceptsRow` 过滤引擎，而非清空模型重新从数据库加载。
- **优化点**:
    - **性能极速**: 搜索操作现在是毫秒级的本地过滤，无模型重置开销。
    - **状态一致性**: 搜索词、颜色、星级等条件现在可以完美逻辑叠加（AND 关系）。
    - **体验优化**: 搜索框清空时不再触发视图回滚，仅重置过滤条件，保持当前目录上下文。

[2026-07-18 12:00:00]
- **任务描述**: 搜索态下地址栏路径回显优化 (Plan-93)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.cpp` (删除 `searchStarted` 槽中的地址栏文本设置逻辑)
- **修改原因**: 消除搜索时地址栏显示“搜索: 关键字”导致的 UI 信息冗余，确保在搜索过程中保留原有的物理路径或分类上下文。
- **优化点**:
    - **信息增值**: 移除与搜索框内容重复的地址栏文本，提升界面价值，保持路径感知一致性。

[2026-07-18 11:45:00]
- **任务描述**: 补全筛选器架构统一后的接口同步修正。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.h/.cpp` (移除 `directoryStatsReady` 信号及统计逻辑中的标签字段)
    - **修改**: `src/ui/MainWindow.cpp` (修正 `populate` 调用参数匹配)
- **修改原因**: 在 Plan-92 中移除了筛选面板的标签区块，导致原有的 7 参数统计信号与新版 6 参数 `populate` 接口不匹配，产生编译错误。
- **优化点**:
    - **接口一致性**: 物理清除了统计链路中已废弃的标签计算，确保编译通过且逻辑闭环。

[2026-07-18 11:30:00]
- **任务描述**: 筛选器架构统一与日期排序交互增强 (Plan-92)。
- **修改文件**:
    - **修改**: `src/ui/FilterPanel.h` (重定义 `FilterState` 结构，新增日期排序状态)
    - **修改**: `src/ui/FilterPanel.cpp` (移除标签区块，实现日期重排逻辑)
    - **修改**: `src/ui/ContentPanel.h/.cpp` (重构 `FilterProxyModel` 逻辑，统一使用 keyword)
    - **修改**: `src/ui/MainWindow.cpp` (简化信号链，合并搜索与过滤状态)
- **修改原因**: 消除搜索与筛选的逻辑双轨制，解决导航重置不彻底的问题，并提升日期筛选的交互效率。
- **优化点**:
    - **架构统一**: 将搜索词合入 `FilterState`，由代理模型单一入口处理，逻辑更清晰。
    - **性能提升**: 移除高负载的标签区块渲染，降低侧边栏内存占用。
    - **交互增强**: 为日期区块引入升降序切换，且在重排时物理保留用户的勾选状态。

[2026-07-18 10:00:00]
- **任务描述**: 修复 MftReader.cpp 中的编译器警告 C4189。
- **修改文件**:
    - **修改**: `src/mft/MftReader.cpp` (删除未使用的局部变量 `currentPath` 和 `identityMatch`)
- **修改原因**: `currentPath` 和 `identityMatch` 变量被初始化但未在后续逻辑中引用，导致 MSVC 编译器抛出 C4189 警告。
- **优化点**:
    - **代码清理**: 移除了冗余的变量声明，保持代码简洁并消除编译警告。

[2026-07-17 16:30:00]
- **任务描述**: 内容面板右键菜单逻辑优化与批量扫描支持 (Plan-80)。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.cpp` (优化 `onCustomContextMenuRequested` 逻辑)
- **修改原因**: 解决多选时出现单选菜单项（重命名）、入库状态感知缺失、以及扫描入库不支持批量操作的问题。
- **优化点**:
    - **菜单动态化**: 基于 `ManagedRole` 自动切换“扫描入库”与“重新扫描入库”，且“重命名”仅在单选时显示，减少误操作。
    - **批量处理增强**: 改造 `ActionAddToCategory` 分支，从选择模型中聚合所有选中路径，实现真正意义上的批量扫描。
    - **交互严谨性**: 路径采集时严格限定 `column() == 0`，规避多列模式下的重复路径采集。

[2026-07-17 15:00:00]
- **任务描述**: 标题栏新增品牌 Logo 功能实现 (Plan-79)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h` (新增 `m_logoLabel` 成员变量)
    - **修改**: `src/ui/MainWindow.cpp` (在 `setupSplitters` 中初始化 Logo 并将其插入标题栏最左侧)
- **修改原因**: 强化品牌识别度，在标题栏 "FERREX" 文本前增加品牌矢量 Logo。
- **优化点**:
    - **视觉平衡**: 采用 18x18px 尺寸，配合 16x16 的 Logo 渲染尺寸，确保在 34px 高度的标题栏中与品牌文字实现视觉对齐。
    - **品牌一致性**: 统一使用 `BrandOrange` (#cb7208) 作为 Logo 与 "FERREX" 文本的颜色，增强品牌感。
    - **布局细节**: 将标题栏前段间距上调至 8px，使 Logo 与文字的呼吸感更自然。
    - **性能友好**: 使用 `UiHelper` 直接从 `SvgIcons` 内联资源加载，无额外文件 I/O。

[2026-07-17 14:30:00]
- **任务描述**: 标题栏布局管理按钮及分栏重置功能实现 (Plan-78)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h` (新增 `m_btnLayout` 成员及 `resetSplitterLayout` 声明)
    - **修改**: `src/ui/MainWindow.cpp` (实现布局按钮点击菜单、重置逻辑，并移除旧有面板右键菜单逻辑)
    - **修改**: `src/core/AppConfig.h` (补全 `remove` 成员函数，修复编译错误)
- **修改原因**: 提升交互确定性，废弃隐蔽且冲突的右键触发模式，改为显式的标题栏按钮控制，并提供一键恢复默认布局的功能。
- **优化点**:
    - **交互收敛**: 彻底取消所有面板容器及标题栏对右键菜单的冒泡触发，避免与子控件右键冲突。
    - **原子重置**: `resetSplitterLayout` 确保先 `show()` 后 `setSizes()`，并同步清除 `AppConfig` 缓存，防止重启回滚。
    - **视觉一致性**: 采用 `layout.svg` 图标，保持 24x24px 标题栏按钮规范。

[2026-07-16 10:45:00]
- **任务描述**: 升级“批量重命名”界面的下拉框（QComboBox）UI。实现圆角设计，并将线条箭头替换为实心向下三角形。
- **修改文件**:
    - `src/ui/RuleRow.cpp`
    - `src/ui/BatchRenameDialog.cpp`
    - `src/ui/SvgIcons.h` (新增紧凑型三角形图标)
- **未修改文件**:
    - `src/ui/BatchRenamePreviewDialog.cpp` (无下拉框组件)
- **优化点**:
    - **UI 参数二次重算**：为追求极致的大气感，将实心三角形图标进一步优化为 12x12 规格，并将 QSS 参数上调（Arrow 12px, DropDown 24px），确保视觉饱满、清晰可见。
    - **布局细节优化**：统一了预设区组件间距为 5px，并将删除按钮调整为持续灰色高亮显示。
    - **性能优化**：引入了 `static const QString` 缓存图标物理路径，解决了 `RuleRow` 在大批量行渲染时的 IO 性能瓶颈。
- **潜在风险**:
    - QSS 中的图标路径在不同操作系统下的兼容性需保持关注（已通过 `UiHelper::getSvgTempFilePath` 处理）。
    - 25px 的锁定高度可能导致三角形图标在某些缩放比例下显示偏小。
[2026-06-17 05:10:51]
- **任务描述**: 确立并记录“范围感知 (Scope-Aware)”核心开发准则，分析双轨制逻辑架构与搜索框重构方案。
- **修改文件**:
    - `AGENTS.md` (记录红线规则)
- **新增文件**:
    - `Analysis_Modification_Plan/Analysis_Modification_Plan-54.md` (架构分析报告)
- **未修改文件**:
    - `src/ui/MainWindow.cpp` (仅分析逻辑)
    - `src/ui/ContentPanel.cpp` (仅分析逻辑)
- **风险提示**:
    - 确立此规则后，后续所有功能开发（当局者身份）必须强制进行“焦点状态检查”，增加了逻辑判断的复杂度，但确保了用户体验的一致性。

[2026-07-xx]
- **任务描述**: 实现 MetadataManager 多维隔离索引架构，并修复搜索结果 UI 渲染断层问题。
- **修改文件**:
    - `src/meta/MetadataManager.h/.cpp`
    - `src/core/CoreController.cpp`
    - `src/ui/MainWindow.cpp`
    - `src/ui/ContentPanel.h/.cpp`
- **核心优化**:
    - **隔离索引架构**：物理分离文件、文件夹及后缀名索引，彻底规避同名冲突。
    - **Loading Token 机制**：为异步加载流程引入令牌校验，解决了“过期扫描任务覆盖最新搜索结果”的严重竞态 Bug。
    - **搜索链路加固**：物理清除搜索时的筛选器干扰，支持流式结果追加。
- **风险提示**:
    - 令牌机制依赖 `m_currentLoadToken` 的单调递增，需确保所有异步加载入口均已接入该机制以防止数据丢失。

[2026-06-19 11:45:10]
任务描述：更新 AGENTS.md 规范（新增实现准则与预警章节），并产出符合新规的话题 5 审计报告。
修改文件：`AGENTS.md` (更新), `Analysis_Modification_Plan/Analysis_Modification_Plan-66.md` (新增)
未修改文件：全部 C++ 源码文件
潜在风险：无。
[2026-06-22] 
- **任务描述**: 实现 Plan-81 到 Plan-85 核心架构优化与功能补全。
- **修改文件**: 
    - **修改**: `src/meta/CategoryRepo.h/.cpp` (新增递归子树获取、多分类聚合查询、FID 分类备份接口)
    - **修改**: `src/meta/MetadataManager.h/.cpp` (递归搜索逻辑适配、标签权重统计接口、MftReader 接口适配)
    - **修改**: `src/core/BasicCommands.h` (新增 `BulkUncategorizeCommand` 撤销指令)
    - **修改**: `src/ui/ContentPanel.cpp` (补全回归未分类的撤销支持逻辑)
    - **修改**: `src/ui/TagManagerView.cpp` (实现“常用标签”动态展示区)
    - **修改**: `src/mft/MftReader.h/.cpp` (USN 更新接口支持原生字节流解析，引入物理身份预检)
    - **修改**: `src/mft/UsnWatcher.cpp` (修正 USN 字段提取偏移)
    - **修改**: `src/ui/BatchProgressDialog.h` (引入 UI 降频刷新策略)
    - **修改**: `src/util/ImportHelper.cpp` (优化导入循环性能，平衡事务开销)
    - **修正**: `src/mft/MftReader.cpp` (修复接口指针类型不匹配、Lambda 捕获缺失及 QDateTime 未定义错误)
    - **修正**: `src/mft/UsnWatcher.cpp` (修复调用 MftReader 接口时的显式类型转换)
- **修改原因**: 解决分类搜索不完整、标签系统缺乏引导、高风险归类不可逆、USN 同步可能存在的身份误嫁接、以及大规模导入时的 UI 响应迟滞等问题。同时修复了由大规模重构引入的编译链断裂。
- **优化点**:
    - **搜索增强**: 实现了真正意义上的侧边栏范围递归搜索。
    - **交互安全**: 为“回归未分类”提供了原子化的 Undo/Redo 保护。
    - **感知优化**: 引入基于频次的标签引导，缩短用户在大规模库中的决策路径。
    - **性能加固**: 通过降频刷新释放主线程消息循环压力，提升极端 IO 下的响应。

[2026-06-22 04:10:00]
- **任务描述**: 修复 MFT 引擎与缓存管理器中的关键编译错误（彻底版）。
- **修改文件**:
    - **修改**: `src/mft/MftReader.cpp` (修正 `updateEntryFromUsn` 函数中的 `usn` 未定义及 `record` 错误引用)
    - **修改**: `src/core/CacheManager.cpp` (补全 `QDateTime` 引用)
    - **修改**: `src/ui/MainWindow.cpp` (补全 `QDateTime` 引用)
    - **修改**: `src/ui/BatchProgressDialog.h` (补全 `QDateTime` 引用)
- **修改原因**: 解决由重构引起的“currentMSecsSinceEpoch 找不到标识符”、“record/usn 未声明”以及“使用了未定义类型 QDateTime”等编译阻塞。
- **优化点**:
    - **作用域修正**: 确保 V3 布局解析正确使用 `recordPtr`，解决了由于 V2 分支变量遮蔽导致的 V3 逻辑无法识别 `record` 的问题。
    - **类型安全**: 正确声明 `USN` 类型变量并补全 V2/V3 全路径赋值，确保 USN 游标持久化数据不丢失。

[2026-06-22 04:30:00]
- **任务描述**: 缩略图/网格卡片交互选区感知增强与批量评分支持 (Plan-86)。
- **修改文件**:
    - **修改**: `src/ui/ThumbnailDelegate.cpp` (优化 `editorEvent` 评分点击逻辑)
    - **修改**: `src/ui/ContentPanel.cpp` (优化 `GridItemDelegate::editorEvent` 评分点击逻辑)
- **修改原因**: 解决在大规模数据整理时，卡片内交互功能（如星级评分）仅作用于单个点击项、无视当前已选中的多个项目的问题。
- **优化点**:
    - **选区广播**: 点击卡片内评分区域时，自动获取 `selectionModel`，将操作同步至选区内所有项目。
    - **操作幂等**: 遍历选区时严格限制 `column() == 0`，确保批量修改逻辑的原子性。
    - **交互一致性**: 统一了缩略图视图与网格视图的底层 Delegate 交互行为。

[2026-06-22 05:00:00]
- **任务描述**: 核心性能重构与主线程 I/O 阻塞消除 (Plan-88)。
- **修改文件**:
    - **修改**: `src/meta/MetadataManager.h/.cpp` (新增异步注册接口、锁粒度优化、死锁修复)
    - **修改**: `src/ui/ContentPanel.cpp` (批量归类流程异步化)
    - **修改**: `src/ui/TagManagerView.cpp` (标签数据库写操作异步化)
- **修改原因**: 解决由于同步 I/O 和大粒度写锁引起的主界面假死，消除递归调用 `debouncePersist` 导致的死锁隐患。
- **优化点**:
    - **全异步链**: 实现了 `registerItemsAsync`，将物理属性同步与视觉提取彻底移至后台线程，并配套 COM 环境支持。
    - **锁外 I/O**: 重构 `ensureActivated` 逻辑，确保 Win32 API 访问在释放锁后执行，极大提升了并发性能。
    - **线程安全**: 为 `TagManagerView` 引入 `QPointer` 保护，确保异步数据库任务完成后能安全刷新 UI。
    - **原子推送**: 通过 `pushToDirty_NoLock` 机制解决了批量重命名标签时的锁递归问题。
2026-06-23: 重构 NavPanel 架构，引入 QScrollArea 全局滚动与流式布局，实现高度自适应的磁盘树与收藏夹分组。 (修改)
2026-06-23: 修复 NavPanel 编译错误：通过 DropTreeView 暴露 rowHeight 接口以支持跨类高度计算。 (修改)
2026-06-23: 优化 NavPanel 自适应布局：移除冗余的“本地磁盘”标题，磁盘树直挂主标题；采用 28px 固定行高算法重构高度同步逻辑。 (修改)
2026-06-23: 修复收藏夹拖拽失效问题：为自适应高度视图设置最小 28px 物理接收区；优化 DropTreeView 视觉反馈，恢复放置指示器。 (修改)
2026-06-23: 深度排查收藏夹拖拽问题：1. 将收藏夹 DragDropMode 提升为 DragDrop 以支持跨视图拖放；2. 在 DropTreeView 与 NavPanel 注入全量拖拽调试日志。 (修改)
2026-06-23: 修复 Logger 编译错误：添加头文件引用，并统一使用 ArcMeta::Logger::log 全路径调用。 (修改)
2026-06-23: 优化自适应高度下的 DND 兼容性：收藏夹改为 Expanding 策略填充余白；禁用滚动区 viewport 接收以透传拖拽事件。 (修改)
2026-06-23: 实施日志滚动治理 (Plan-97)：引入 4MB 容量哨兵与双文件滚动机制；优化 Logger 与 main 启动自检逻辑，限制日志总量。 (修改)
2026-06-23: 实现侧边栏“我的分类”专属过滤搜索 (Plan-98)：引入 CategoryFilterProxyModel 递归代理模型；底部新增 8px 圆角搜索框；Delegate 补全 PrimaryBlue 匹配高亮逻辑。 (新增)
2026-06-23: 修复侧边栏搜索代理模型编译错误：在 CMakeLists.txt 中注册 CategoryFilterProxyModel.h 以触发 MOC 生成。 (修改)
2026-06-23: 优化侧边栏搜索框视觉样式：移除冗余容器与分割线；缩减左侧图标间距至 24px；设置 margin 实现紧凑布局。 (修改)
2026-06-23: 修复编译警告：在 Logger.h 中显式忽略 QFile::open 返回值 (C4834)；在 CategoryFilterProxyModel.h 中将 invalidateFilter 替换为 begin/endFilterChange (C4996)。 (修改)
2026-06-23: 分类面板搜索框 UI 归一化与间距修正 (Plan-97)：高度调整为 32px，圆角回归至 6px，修正 padding-left 以实现 8px 视觉间距，移除冗余顶部分割线，并将 select 图标替换为 filter_funnel_outline。 (修改)
2026-06-23: 全局颜色规范修正与解耦 (Plan-98)：在 StyleLibrary 中分离品牌色 BrandOrange (#cb7208) 与置顶激活色 ActiveOrange (#ff551c)，解耦 UI 组件引用，同步更新 Memories.md 物理标准。 (修改)
2026-06-23: 颜色筛选增强：引入“面积占比”双轴过滤逻辑 (Plan-97)：在 FilterPanel 中新增“占比”滑杆，并重构 FilterProxyModel 颜色判定算法，支持按符合色差要求的色块累计面积占比进行高精度过滤。 (修改)
2026-06-23: 筛选滑杆交互增强 (Plan-98)：为面积占比滑杆实现实时 ToolTip 回显，支持悬停及滑动时通过 ToolTipOverlay 动态展示百分比数值。 (修改)
2026-06-23: 筛选器锁定（Pin）功能实现 (Plan-98)：在 FilterPanel 标题栏新增锁定按钮，支持跨目录保留筛选状态，并规范了 force 重置逻辑。 (新增)
2026-06-23: 修复编译错误与滑杆反馈增强 (Plan-98)：修正 ActiveOrange 的命名空间引用（Style::ActiveOrange）；完善面积占比滑杆在滑动/悬停时的 ToolTip 实时百分比数值回显。 (修改)
2026-06-23: 筛选重置逻辑优化 (Plan-98)：当执行“重置所有筛选条件”时，同步恢复筛选锁定 (Pin) 到默认未激活状态。 (修改)


## 2026-06-26
- **任务描述**: 导航面板交互优化与状态持久化。
- **修改文件**:
    - **修改**: src/ui/NavPanel.cpp
- **修改原因**: 修复收藏夹标题悬停变色、禁用非预期折叠行为，并实现 Splitter 布局的自适应与持久化。
- **优化点**:
    - **视觉修正**: 移除收藏夹标题悬停时的白色变色，保持品牌色一致性。
    - **交互稳健性**: 禁用标题点击折叠功能，防止标题与内容区域视觉分离。
    - **布局自适应**: 引入 restoreState 与 saveState 机制，允许用户手动调整比例并持久化；通过 setStretchFactor 实现磁盘树与收藏夹的“智能”空间分配。

- **任务描述**: 预览功能回归修复与样式规范化 (Plan-109 补丁)。
- **修改文件**:
    - **修改**: src/ui/QuickLookWindow.cpp
- **修改原因**: 解决 PNG/EPS 等格式在特定环境下加载失效的问题，并统一预览窗口滚动条视觉规范。
- **优化点**:
    - **分流降级机制**: 为标准图像加载引入“原生 -> Shell 引擎”降级链路，为专业格式引入“Shell -> 原生”保底链路，确保 PNG/EPS 100% 可视。
    - **样式全局化**: 滚动条 QSS 样式（10px, 3px radius）提升至窗口级，消除子组件样式碎片化，确保图像与文本预览视觉高度统一。
    - **精准选择器**: 引入 `#QuickLookWindow` 标识符，防止样式污染其他 UI 组件。
    - **极致画质 (抗锯齿优化)**: 
        1. 移除了 `setDevicePixelRatio` 调用，消除了由 DPI 适配诱发的二次插值锯齿。
        2. 引入了基于 `m_graphicsView` 尺寸的 `QImage::scaled(SmoothTransformation)` 预缩放机制，利用面积平均采样算法确保渲染前图片像素已与显示器逻辑点完美契合。
        3. 实现了标准图像与专业格式（EPS/AI）画质链路的同步强化。
    - **布局修正**: 将 `QuickLookWindow` 主布局边距由 (10,10,0,10) 修正为 (0,0,0,0)，彻底解决了图片预览不居中的视觉偏差。
    - **SVG 矢量渲染**: 针对 SVG 格式引入了 `QSvgRenderer` 专属渲染路径，绕过系统图标干扰，实现 1:1 矢量内容还原。

## 2026-11-15
- **任务描述**: 修复 AutoImportManager 链接与编译错误。
- **修改文件**:
    - **修改**: `CMakeLists.txt` (在 SOURCES 中添加 `src/core/AutoImportManager.cpp` 和 `src/core/AutoImportManager.h`)
    - **修改**: `src/core/AutoImportManager.cpp` (补全头文件、修复宏冲突及消除 C4858 警告)
- **修改原因**: 解决由于 `AutoImportManager` 源文件缺失、Windows 环境宏冲突及 `[[nodiscard]]` 返回值被丢弃导致的编译链接失败与警告。
- **优化点**:
    - **构建稳定性**: 确保项目核心模块被正确编译链接，并实现“零警告”构建。
    - **环境兼容性**: 引入 `#undef run` 防御 Windows SDK 宏污染，并补全 `QtConcurrent` 与 `QDateTime` 依赖。

- **任务描述**: 消除选中项目时的磁盘访问延迟 (Plan-110)。
- **修改文件**:
    - **修改**: `src/core/ModelContract.h` (新增 FileSize/Mtime/Ctime/Atime/IsDir/SuffixRole 扩展角色)
    - **修改**: `src/ui/ContentPanel.cpp` (在 `data()` 中补全对新增扩展角色的缓存数据支持)
    - **修改**: `src/ui/MainWindow.cpp` (重构 `selectionChanged` 回调，彻底废除 `QFileInfo` 同步磁盘调用)
- **修改原因**: 解决点击选中项目时由于主线程同步调用 `QFileInfo` 导致的 UI 卡顿或冻结问题。
- **优化点**:
    - **零磁盘 I/O**: 属性面板所需的所有物理元数据（大小、时间、类型等）现在全部从内存模型缓存中读取，点击响应从“磁盘级”提升至“内存级”。
    - **性能倍增**: 即使在 HDD 或网络共享路径上，选中操作也能保持丝滑响应，彻底消除了连续 8 次系统 API 调用带来的主线程阻塞。

- **任务描述**: 修复托管库自动入库失效问题 (Plan-108)。
- **修改文件**:
    - **修改**: `src/core/AutoImportManager.cpp` (增加默认路径 `ArcMeta.Library_[DriveLetter]` 保底识别逻辑)
    - **修改**: `src/ui/MainWindow.cpp` (补全托管库创建时的配置持久化，实现“重新扫描”动作的物理全量扫描逻辑)
- **修改原因**: 解决托管库激活后无法感应存量文件自动入库的问题。
- **优化点**:
    - **逻辑闭环**: 实现了从物理扫描、FRN 提取到缓冲区注入、异步任务触发的完整入库链路。
    - **稳定性**: 通过保底识别逻辑确保了即使在配置丢失的情况下，监听引擎仍能正确识别托管文件夹。

- **任务描述**: 托管库入库逻辑与 USN 监听定点修复 (Plan-111)。
- **修改文件**:
    - **修改**: `src/core/AutoImportManager.cpp` (修复路径斜杠匹配、重构 startTask、实现实质暂停、落实单层级监听)
    - **修改**: `src/core/AutoImportManager.h` (引入 `m_globalPaused` 原子标志位)
- **修改原因**: 解决路径斜杠不一致导致的监听恒失效问题，修复执行流绕过 `ImportHelper` 的违规行为，并实现单层级监听约束与任务中断控制。
- **优化点**:
    - **匹配校准**: 通过 `QDir::toNativeSeparators` 物理对齐 Windows 路径格式，使托管文件夹判定逻辑恢复正常。
    - **规范重构**: `startTask` 现在批量调用 `ImportHelper::importPaths`，且通过 `QMetaObject::invokeMethod` 保证跨线程 UI 调用安全。
    - **监听瘦身**: 落实“单层级监听”红线，仅标记托管文件夹直属项，大幅降低缓冲区条目密度。
    - **中断可控**: 实现了基于原子标志位的 `pauseTask` 逻辑，支持入库循环的即时响应与中断恢复。

- **任务描述**: 修复 MainWindow.cpp 磁盘对账逻辑编译错误。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.cpp` (补全 `#include "sqlite3.h"`)
- **修改原因**: 解决在执行磁盘托管库物理对账扫描时，由于未包含 SQLite 头文件导致原生 API 无法解析的问题。
- **优化点**:
    - **接口完整性**: 确保 `MainWindow` 的磁盘对账 Lambda 任务能正确操作 `pending_imports` 缓冲区。
    - **警告消除**: 物理清理了 1371 行的 `C4858` 警告，实现了全量构建“零警告”。

- **任务描述**: 异步高性能恢复文件夹登记进度环。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.h/.cpp` (引入单线程池队列扫描，使用 QDirIterator 实现异步进度计算)
- **修改原因**: 恢复因性能优化而暂时移除的文件夹“登记进度环”功能，通过异步、串行、高效枚举方式，在不阻塞 UI 的前提下精准显示文件夹内项目的登记比例。
- **优化点**:
    - **串行 I/O 防御**: 采用 `maxThreadCount = 1` 的专属线程池，避免多目录同时扫描引起的磁盘寻道竞争。
    - **高效枚举**: 使用 `QDirIterator` 替代传统递归，极大降低了内存分配开销并提升了扫描速度。
    - **增量刷新**: 任务完成后通过 `RegistrationProgressRole` 触发局部重绘，确保 UI 更新丝滑无抖动。

## 2026-11-16
- **任务描述**: 修复 ContentPanel.h 缺少头文件导致的编译错误。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.h` (补全 `<atomic>`, `<QSet>`, `<QMutex>`, `<QThreadPool>`)
- **修改原因**: 修复“缺少类型说明符”、“未知重写说明符”以及“未声明的标识符”等编译错误。
- **优化点**:
    - **编译稳固**: 确保 `ContentPanel` 内部使用的异步进度扫描组件类型被正确识别，实现了全量构建通过。

- **任务描述**: 修复 QuickLookWindow 预览图片居中偏置问题。
- **修改文件**:
    - **修改**: `src/ui/QuickLookWindow.cpp` (重构锚点管理逻辑)
- **修改原因**: 修复因缩放操作后 `TransformationAnchor` 保持为 `AnchorUnderMouse` 导致 `fitInView` 计算偏置的问题。
- **优化点**:
    - **居中稳定性**: 确保在加载新图片前及缩放结束后，锚点强制归位至 `AnchorViewCenter`，保证预览始终居中。

- **任务描述**: 修复托管库自动入库状态更新失效问题。
- **修改文件**:
    - **修改**: `src/core/AutoImportManager.cpp` (重构 `startTask` 中的 SQL 更新逻辑)
- **修改原因**: 修复因使用 `frn` 而非主键 `path` 作为 `UPDATE/DELETE` 条件导致的状态同步失效。
- **优化点**:
    - **数据一致性**: 确保 `pending_imports` 状态能正确流转至已完成 (2)，防止重复入库任务的无效执行。

- **任务描述**: 修复托管库自动入库 `startTask` 死锁问题。
- **修改文件**:
    - **修改**: `src/core/AutoImportManager.cpp` (移除 `invokeMethod` 对 `importPaths` 的包裹)
- **修改原因**: 修复 `Qt::BlockingQueuedConnection` 导致后台线程与主线程循环等待引发的界面假死。
- **优化点**:
    - **并发稳定性**: 消除死锁隐患，恢复“扫描该盘”功能的响应性，确保自动入库任务能平滑执行。

[2026-11-16 11:30:00]
- **任务描述**: 修复 UI 代理及导入助手编译错误与变量重定义。
- **修改文件**:
    - **修改**: `src/ui/ContentPanel.cpp` (移除 `GridItemDelegate::paint` 中 `path` 变量的重复定义)
    - **修改**: `src/ui/ThumbnailDelegate.cpp` (补全 `AutoImportManager.h` 头文件引用)
    - **修改**: `src/ui/TreeItemDelegate.h` (补全 `AutoImportManager.h` 头文件引用)
    - **修改**: `src/util/ImportHelper.cpp` (补全 `ShellHelper.h` 头文件引用)
- **修改原因**: 解决由于标识符未定义及变量多次初始化导致的编译阻塞。
- **优化点**:
    - **编译稳固**: 确保所有 UI 代理均能正确识别 `AutoImportManager` 接口，并消除 MSVC 编译错误。

- **任务描述**: 托管库核心架构重构与物理同步一体化 (Plan-113)。
- **修改文件**:
    - **修改**: `src/meta/DatabaseManager.cpp` (新增 `ingestion_status` 字段，废除 `pending_imports` 表)
    - **修改**: `src/meta/MetadataManager.h/.cpp` (同步内存模型，实现状态设置接口与 SQL 持久化)
    - **修改**: `src/core/AutoImportManager.h/.cpp` (重构监听逻辑，实现“迁移即激活”与托管库自愈)
    - **修改**: `src/ui/CategoryModel.cpp` (移除“未分类”冗余项，实现逻辑-物理重命名同步；补全 `ShellHelper.h`)
    - **修改**: `src/ui/ContentPanel.cpp` (更新 Grid 渲染逻辑，实现 Registered/Invalid 视觉反馈；库外导入强制迁移；补全 `AutoImportManager.h`)
- **修改原因**: 确立以“托管库”为核心的数据管理体系，解决 I/O 资源盲目竞争、逻辑链路冗余以及物理变更感应滞后的问题。
- **优化点**:
    - **状态机闭环**: 引入 Registered (占坑) -> Ingested (受控) -> Invalid (失效) 完整流转逻辑，UI 响应由数据库状态直接驱动。
    - **物理强准入**: 实现库外项目导入时的自动物理迁移，确保管理入口唯一性。
    - **冗余根除**: 物理清理“未分类”虚拟逻辑，实现侧边栏分类名与物理磁盘目录名的实时双向同步。
    - **失效保护**: 采用半透明灰度样式保留失效项目元数据，支持 FRN 启发式重识别与自愈。
