# 开发需求台账 Development_Plan.md

## [2024-07-20] 扩大范围排查当前版本的整体逻辑架构

- 用户描述的现象/问题：当前版本的整体逻辑架构可能存在职责过载、运行流程上下文冲突、重复造轮子、性能问题（死循环、线程竞争、假死、卡顿）以及线程相互干扰。
- 用户期望的结果：扩大范围排查这些潜在架构及运行问题，并将排查到的结果详细记载到 `Inspect and Mark.md` 文档里。
- 本次任务边界：对当前版本的整体逻辑架构展开全面的代码审计与静态排查，针对指定5点列出详细排查结果，格式化输出到 `Inspect and Mark.md` 中。因为当前角色是分析师，仅对代码逻辑及架构进行审计分析，不修改任何功能性代码文件。
- 不在本次范围内的是：实际修改代码或引入任何功能性代码的改动。
- 对应方案文档：无（本次为分析委托，直接产出 `Inspect and Mark.md` 文档，不需要单独编写 `Modification_Plan-N.md` 并执行代码修改，因为没有代码修改需求）

## [2026-07-20] 缩略图“先显示后秒消失”故障 analysis 与自适应缓存容量优化

- 用户描述的现象/问题：在内容面板中显示数据时（尤其是SVG或图像等图形文件），缩略图会先短暂显示，随后在1秒内瞬间消失并退化为纯灰色占位符。
- 用户期望的结果：分析该现象并给出彻底根治的逻辑架构方案，同时评估 QCache 的 LRU 淘汰机制是否为故障根因，重新设计一套能与虚拟滚动可见区域动态关联、避免在常见规模文件夹下产生缩略图淘汰的自适应动态缓存容量公式，并在超大/百万级文件夹下做合理的防御性妥协。
- 本次任务边界：对 `src/ui/ContentPanel.cpp` 中的 `FerrexVirtualDbModel` 缩略图加载与缓存机制进行根因定位。定位并重构后台提取线程中直接操纵 GUI 敏感 `QPixmap` 导致的硬件句柄失效 Bug。在 `setRecords`（初次加载）与 `loadThumbnailsForRows`（滚动视口刷新）双阶段中同步引入自适应 `maxCost` 计算逻辑，确保无损与防御性限幅。
- 不在本次范围内的是：修改物理 MFT 扫描、USN 监控底座以及其他非缩略图相关的逻辑。
- 对应方案文档：Modification_Plan-33.md

## [2026-07-20] 内容面板视图模式对齐重构与自定义调整滑杆及视图按钮移植

- 用户描述的现象/问题：当前版本现有的三个视图模式没有达到预期；且原有Ctrl+滚轮缩放兼切换多视图的逻辑未达预期；同时顶部缺少方便调节卡片尺寸 and 快速切换视图排列方式的控件。
- 用户期望的结果：在当前版本中移植并实现类似于 FERREX-META 版本的“调整卡片尺寸的滑杆（m_sizeSlider）” and “排列方式 of 视图按钮（viewBtn）”；完全放弃原有Ctrl+滚轮多级缩放切换视图的逻辑；且参照 FERREX-META 的实现彻底重构移植三个视图模式（列表、自适应、网格），确保职责单一模块化。
- 本次任务边界：重写并替换当前版本的 `ListResultView`、`GridResultView`、`JustifiedResultView` 等视图接口与实现，剔除不合预期的多级滚轮缩放切换逻辑；并在 `ContentPanel` 顶部标题栏区域中嵌入 `m_sizeSlider` 与 `viewBtn` 控件，实现卡片尺寸 32~256 像素无缝调节及列表/自适应/网格三种排版模式的一键式极速切换。
- 不在本次范围内的是：修改 MFT / USN 底层扫描或数据库检索流程，移植与视图显示无关 of 外部控制器组件。
- 对应方案文档：Modification_Plan-34.md

## [2026-07-20] 三种视图架构设计缺陷排查与极致重构规划

- 用户描述的现象/问题：当前版本中的三种视图（列表、网格、合理自适应对齐模式）存在 logic 冗余、职责过载以及交互/渲染卡顿等傻逼逻辑架构问题。
- 用户期望的结果：对三种视图底层控制链展开深度排查，找出不合理的架构缺陷并形成极致解耦与性能提升 of 优化方案，并产出详细设计。
- 本次任务边界：审计并理清 `JustifiedView`、`ListResultView`、`GridResultView`、`JustifiedResultView` 以及 `ContentPanel` 之间在视图控制、数据映射 and 布局渲染上的依赖关系，设计职责更清晰、耦合度更低的模块化结构。
- 不在本次范围内的是：实际修改或编译代码，移植非视图类的其他外部功能。
- 对应方案文档: Modification_Plan-35.md

## [2026-07-20] 彻底放弃当前 QuickLookWindow 运行逻辑并使用 FERREX-META 逻辑替代重构

- 用户描述的现象/问题：当前版本的 `QuickLookWindow` 预览运行逻辑不符合预期，其处理逻辑 and 界面交互（如图像缩放、音视频媒体处理、文本二进制检测及编码识别等）功能不够完备。
- 用户期望的结果：彻底放弃当前版本的 `QuickLookWindow` 运行逻辑，将其替换为 `FERREX-META` 版本中的 `QuickLookWindow` 运行逻辑。这包括采用专门设计的 `QuickLookGraphicsView`（支持滚轮缩放、双击恢复/适配大小、鼠标拖拽移动及光标跟随变动）、引入更完整的多媒体及文本二进制与编码自动检测识别渲染，实现更好的大文件和特殊文件类型的预览。
- 本次任务边界：彻底重构 `src/ui/QuickLookWindow.h` and `src/ui/QuickLookWindow.cpp`，将 `FERREX-META` 版本的 `QuickLookWindow` and `QuickLookGraphicsView`控制与交互机制移植并合并至其中，适配 `ArcMeta` 命名空间及符合本项目标准的样式设计。同时确保当前项目正在运行的核心快捷键/信号链路继续工作（评分打标 `ratingRequested`、颜色标签打标 `colorRequested` 以及切图 `prevRequested`/`nextRequested` 信号），并对音视频进行优雅的占位降级兼容。
- 不在本次范围内的是：修改 `MainWindow` 的主导航调度机制 or 任何非预览关联的面板组件。
- 对应方案文档: Modification_Plan-36.md

## [2026-07-21] 预览界面滚动条样式对齐考古规范优化

- 用户描述的现象/问题：按下空格键打开预览界面后的滚动条样式，违背了考古，由上一代 AI 导致。
- 用户期望的结果：将预览界面（QuickLookWindow）内的滚动条样式彻底重构，使其严格遵循全局规范：宽度 10px、圆角 3px、背景透明、Handle 颜色对齐 `BorderColor` #333333。
- 本次任务边界：修改 `src/ui/QuickLookWindow.cpp` 中 `QuickLookGraphicsView` 和 `QPlainTextEdit` 控件的水平及垂直滚动条的 QSS 样式。
- 不在本次范围内的是：修改预览界面其他的图片/文本渲染、缩放或事件分流逻辑.
- 对应方案文档: Modification_Plan-37.md

## [2026-07-21] 预览界面多媒体残留代码彻底物理根除与净化

- 用户描述的现象/问题：上一代 AI 在收到彻底根除多媒体播放功能的指令后，依然在 `QuickLookWindow` 中保留了多媒体播放及占位相关冗余代码。
- 用户期望的结果：彻底删除并根除 `QuickLookWindow` 中的所有多媒体组件（如播放、暂停、进度条、多媒体相关容器控件 and 占位框等）和无用快捷键与接口，音视频转走标准系统大图标降级展示。
- 本次任务边界：彻底删除 `src/ui/QuickLookWindow.h` 与 `src/ui/QuickLookWindow.cpp` 中的所有多媒体成员变量、宏定义、多媒体容器构建、以及 `renderMedia`、`resetMedia`、键盘 `P` 按键处理逻辑等。
- 不在本次范围内的是：修改预览界面其他的图片/文本渲染、缩放或事件分流逻辑。
- 对应方案文档: Modification_Plan-38.md

## [2026-07-21] 预览窗快捷键 ToolTip 屏幕上方居中淡进淡出显示、切图/导航加固及 ContentPanel 选中联动

- 用户描述的现象/问题：
  1. 在执行“QuickLookWindow”里的评分（1-5）和颜色标记（Alt+1-9）快捷键时，其 ToolTip 提示显示在偏角（QPoint(50,50)），而不是靠齐屏幕上方居中，且不具备优雅的淡进淡出（Fade In/Out）效果。
  2. 切图/导航功能（Up/Left 切上一张，Down/Right 切下一张）失效，按下按键无响应。
- 用户期望的结果：
  1. 支持将 ToolTip 提示在屏幕上方居中且自带淡进淡出的视觉效果展现出来。
  2. 修复切图/导航快捷键响应。按下 Arrow keys 时能精准跳转上一个或下一个文件，并让主界面的 ContentPanel 对应的高亮选中项也同步更新滚动。
- 本次任务边界：
  1. 在 `ToolTipOverlay` 中引入 QPropertyAnimation，实现 `windowOpacity` 属性动画（淡进淡出 150ms 优雅呈现），并新增 `exactPosition` 精确定位参数。
  2. 在 `QuickLookWindow::eventFilter` 中加装事件过滤器物理拦截 `m_graphicsView` 和 `m_textEdit` 的 `KeyPress` 动作，拦截 arrow keys、数字、Alt 等核心快捷键并转发分发给 `keyPressEvent`。
  3. 在 `ContentPanel` 中新增 `selectAndScrollToPath(path)` 方法，保持主视图选中态随切图进行双向联动。
- 不在本次范围内的是：重构预览界面 or 主界面的非快捷键/切图逻辑。
- 对应方案文档: Modification_Plan-39.md

## [2026-07-21] ThumbnailDelegate 职责过载审计与模块化拆分规划

- 用户描述的现象/问题：单元格展示委托 `ThumbnailDelegate` 存在多维重绘、生命期、排版 and 提示的职责过载，难以维护及复用。
- 用户期望的结果：对其进行代码职责过载分析并输出模块化的单一职责拆解和落地规划。
- 本次任务边界：对 `src/ui/ThumbnailDelegate.h` 与 `src/ui/ThumbnailDelegate.cpp` 展开整体架构梳理，定位其过载现状，设计面向高内聚、零开销、低耦合的模块化（视觉物理渲染、文本排版、控制器生命期分流）拆分图纸。由于本 Turn 为只读分析师模式，不修改任何项目代码文件。
- 不在本次范围内的是：修改逻辑代码、在外部执行构建验证。
- 对应方案文档：Modification_Plan-40.md

## [2026-07-22] 视图按钮及缩放滑杆功能移植重构

- 用户描述的现象/问题：当前版本缺少直观的“缩放滑杆” and “视图模式一键切换”按钮，且 Ctrl+滚轮 缩放无法动态调整视图卡片的大小，交互体验滞后。
- 用户期望的结果：将 FERREX-META 版本的滑杆（m_sizeSlider）和排列方式视图按钮（viewBtn）移植到当前版本并关联使用，同时支持通过 Ctrl+滚轮 在内容视图上进行自由的比例缩放。
- 本次任务边界：
  1. 在主窗口 `MainWindow::setupCustomTitleBarButtons()` 对应的自定义按钮组中，植入“缩放滑杆”和“视图模式一键切换按钮”，调整按钮间距物理对齐全局规范。
  2. 重构 `ContentPanel` 的 `wheelEvent`，捕获 Ctrl+滚轮 动作实现滑杆尺寸/内容卡片尺寸 `m_zoomLevel`（限制在 96~128px 之间）的物理同频缩放，并保证滑杆值及 `updateGridSize()` 持久化一致。
  3. 在“视图按钮”下拉菜单中，完美对接 `ListView`、`GridView` (网格自适应即 JustifiedMode) 以及 `JustifiedViewMode` 的 logic 切换。
- 不在本次范围内的是：重写或修改物理 USN 监控底座或底层 MFT 文件过滤搜索流程。
- 对应方案文档：Modification_Plan-45.md

## [2026-07-24] 全款应用架构与文件职责过载深度排查

- 用户描述的现象/问题：应用中可能存在整体混乱的逻辑架构，文件职责过载（违反SRP），以及存在某些功能具有不合理的傻逼逻辑架构。
- 用户期望的结果：对全款应用及核心源码文件进行地毯式的通红与细粒度审计，标记所有存在职责过载（违反 SRP）的文件，定位逻辑架构设计不合理的傻逼逻辑架构功能。
- 本次任务边界：对 `src/` 目录下的核心逻辑、业务模块 and UI 骨架（如 `ContentPanel`, `MainWindow`, `MetadataManager`, `MftReader` 等）及各组件数据流向、跨层耦合 and UI 层业务硬编码进行地毯式地深层审计与排查，输出架构漏洞、混乱点、文件职责过载证据（行号 + 代码片段）并归档。
- 不在本次范围内的是：修改任何代码文件、创建实际重构代码、或扫描非当前项目指定的外部不相关目录。
- 对应方案文档：Modification_Plan/Modification_Plan-54.md

## [2026-07-25] 重复多媒体及色彩解析清理与兜底重扫描路径加固

- 用户描述的现象/问题：在内容面板中，存在重复造轮子的“重新解析颜色”功能（ActionExtractColor），它绕过了底层的统一数据提取管线且采用残缺实现，导致了字段污染与维护开销；而现存的“重新扫描”功能作为用户的手动兜底机制是必要且符合预期的。
- 用户期望的结果：彻底物理删除绕过底层提取管线的“重新解析颜色”功能分支与界面入口；同时，保留并维持“重新扫描”采用统一 `registerItemsAsync` -> `MediaExtractorPipeline` 异步处理流程及状态回置的完美机制。
- 本次任务边界：
  1. 在 `src/ui/ContentPanel.cpp` 中完全删除 `ActionExtractColor` 对应的右键菜单项及相关的后台 `QtConcurrent::run` 批量调色板解析/UI通知等逻辑。
  2. 保护 `ActionRescan`（重新扫描）逻辑并维持现有调用，确保 `updateIngestionStatus(nPath, 0)` 对视觉反馈的更新。
- 不在本次范围内的是：修改核心统一多媒体解析逻辑 `MediaExtractorPipeline` 及 `registerItemsAsync` 的底层排队管道。
- 对应方案文档：Modification_Plan/Modification_Plan-60.md

## [2026-07-25] 文件夹与映射分类颜色双向实时同步机制修复

- 用户描述的现象/问题：之前版本原本支持对文件夹设定颜色标签，但在前几轮修改时由于jules脑补修改导致同步机制被破坏。具体为：对内容区物理文件夹设定某个颜色标签后，无法同步到相应的映射分类颜色；同样地，从侧边栏对分类设定颜色后，也无法同频更新对应入库的物理文件夹颜色。
- 用户期望的结果：无论是从侧边栏分类设定颜色或从内容面板对文件夹进行设定颜色，两者都能相互同步展现同频的颜色标签。该功能仅限与已入库的文件夹/分类。
- 本次任务边界：
  1. 在 `MetadataManager::setColor` 内部增加同步判断，一旦对文件夹修改颜色，同步调用 `CategoryRepo::updateCategoryColorByPath` 将颜色同步更新至映射分类定义中。
  2. 在 `CategoryPanel::onSetColor` and `onRandomColor` 中增加同步写入，当分类绑定的物理文件夹非空时，同步调用 `MetadataManager::instance().setColor` 将新颜色同步写入对应的物理文件夹缓存与持久化中。
  3. 确保该双向同步动作仅对已入库的物理文件夹/分类有效，不影响其他普通未入库项目。
- 不在本次范围内的是：修改非颜色属性的元数据持久化、或者对非入库文件更改颜色的独立打标逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-61.md

## [2026-07-25] 彻底根除随机/设置颜色旧UI与在右键菜单上直接以悬停白圈色块展示并同步存库

- 用户描述的现象/问题：现有的右键菜单及部分分类/文件夹设置中采用“随机颜色”和“设置颜色”的传统菜单项（QAction）及弹窗操作链路复杂、不够直观。
- 用户期望的结果：彻底根除“随机颜色”和“设置颜色”相关逻辑代码，不可保留。直接将当前现有的标注色直接显示在右键菜单上作为一排水平排列的色块进行选择，悬停在某个色块上方时通过白圈突显色块。选择色块后，将色值同时存入到 categories 表的 color 字段和 metadata 表 of color 字段中。
- 本次任务边界：
  1. 彻底删除 `CategoryPanel` 及 `MainWindow`（FolderButton 菜单）中的“随机颜色”与“设置颜色”旧 QAction 和旧对应响应函数（如 `onRandomColor` / `onSetColor`）。
  2. 针对 `CategoryPanel` 侧边分类右键菜单、`MainWindow` 中的 FolderButton 右键菜单，以及 `ContentPanel` 内容右键菜单中的“设定颜色标签”（ActionColorTag）子菜单，实现全新的一排水平排列色块快捷操作区域（可以使用自定义 `QWidgetAction` 加载色块容器）。
  3. 每个色块尺寸 and 间距根据界面情况合理设定（如圆形色块，直径 20px-24px，间隔 6px-8px），且鼠标悬停在色块上方时绘制出明晰的白色同心圆突显色块边缘。
  4. 选中某个色块后，立即调用更新接口：
     - 如果是在 `CategoryPanel` 对分类进行设置，将色值写入对应分类在 `categories` 表的 `color` 字段，同时如果该分类绑定了物理文件夹路径，则将对应路径在 `metadata` 表的 `color` 字段也一并同步更新；
     - 如果是在 `MainWindow` 对 FolderButton 自定义 monitored folder 进行操作，将色值写入 AppConfig 中对应属性，并更新对应物理路径在 `metadata` 表的 `color` 字段（如果已登记到库中，也会同步到绑定映射分类）；
     - 如果是在 `ContentPanel` 对登记的项目进行操作，将色值写入该项目在 `metadata` 表的 `color` 字段，若该项目是已入库且绑定了侧边栏映射分类的文件夹，也将色值存入对应分类的 `categories` 表 of `color` 字段中。
- 不在本次范围内的是：修改非颜色属性的其他关联持久化，或者调整与色块绘制无关的其他右键菜单项。
- 对应方案文档：Modification_Plan/Modification_Plan-62.md

## [2026-07-26] 解决全局所有按钮点击后显示点状聚焦虚线框问题

- 用户描述的现象/问题：在全站范围内，当用户点击任意按钮时，按钮周边都会显示点状虚线聚焦边框。
- 用户期望的结果：全局禁用这一虚线的显现，保持全站级的完美屏蔽，不影响原有样式且实现最简单、彻底的修复。
- 本次任务边界：在 `src/main.cpp` 中初始化 `QApplication` 的位置，加入一行全局样式表设置 `a.setStyleSheet("QAbstractButton { outline: none; }");` / `a.setStyleSheet("QAbstractButton { outline: none; }");` 的统一设置。
- 不在本次范围内的是：逐个修改各页面组件按钮的逻辑，或在其他样式表文件中写分散的规则。
- 对应方案文档：Modification_Plan/Modification_Plan-91.md

## [2026-07-26] main.cpp 打补丁式架构排查与极致重构规划

- 用户描述的现象/问题：`main.cpp` 被写入了大量应急性补丁、跨线程单例强制点名预热、同步高频物理日志落盘，导致职责过载、单实例竞态及程序退出强杀数据库、拖慢扫描等严重隐患。
- 用户期望的结果：消除这些典型的打补丁式脏代码，按照工业级标准对程序启动、优雅退出、多线程协作生命期与异步日志进行极致重构。
- 本次任务边界：
  1. 重写 `src/main.cpp` 编排科学时序启动、退出关于 aboutToQuit 信号的清场槽函数。
  2. 重构 `src/ui/Logger.h` 实现高性能异步缓冲日志线程（去除 qDebug 的同步物理写锁阻塞）。
  3. 修改 `src/core/CoreController` 引入 `initializeCoreComponents`，统一、拓扑性地进行单例实例化及定时器哑死物理加固。
- 不在本次范围内的是：修改物理 MFT 文件扫描核心算法、IOCP 底层驱动及 UI 具体的渲染重排。
- 对应方案文档：Modification_Plan/Modification_Plan-89.md

## [2026-07-26] 磁盘模式下重命名导致缩略图变灰设计缺陷修复

- 用户描述的现象/问题：在磁盘导航模式下打开某个文件夹后，对其中的某个图形文件执行重命名操作。重命名成功后，由于新旧物理路径发生变更，但缩略图及宽高比缓存未同步迁移或清理，导致该文件在重置渲染时无法命中缓存，变为了纯灰色占位符。
- 用户期望的结果：重命名成功后，缩略图和宽高比等高级元数据缓存应当无缝继承 to 新文件名路径下，不发生变灰与闪烁，保证无缝的高性能浏览体验。
- 本次任务边界：
  1. 在 `FerrexVirtualDbModel::setData` 异步重命名成功的主线程回调中，针对 `m_iconCache`（缩略图缓存）与 `m_aspectRatios`（宽高比缓存）执行高内聚的 **无损缓存 Key 迁移**。
  2. 将原保存在旧路径 `oldPath` 下的 `QIcon` 指针及 `m_aspectRatios` 值，安全移除并重新插入 to 新路径 `nativeNewPath` 的 Key 下，无需重新进行后台多媒体解析或读盘。
- 不在本次范围内的是：修改物理 MFT 扫描、USN 监控底座以及其他非缩略图相关的逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-94.md

## [2026-07-26] 行内编辑及全选状态下方向键选择漂移与光标一键定位修复

- 用户描述的现象/问题：当选中某个项目并按下 F2 进入行内编辑时，
  1. 已经进入了编辑状态，用户按下向上/向下方向键时，意外游动并选中了上方/下方的项目，导致重命名编辑被意外中断并退出。
  2. 如果用户在全选（高亮）状态下按下向左/向右方向键，光标会尴尬地定位到扩展名“.”的后面，而不是文本最前或最后。
- 用户期望的结果：
  1. 在已经进入编辑状态后，按下向上/向下键应被完全屏蔽拦截，绝对不向上或向下游动选中邻近项目。
  2. 全选状态下，按向左方向键应一键将光标精准定位到最前面（位置 0）并清除选中，按向右方向键应一键将光标精准定位到包含扩展名在内的最末尾（位置 length()）并清除选中。非全选状态下则恢复普通的逐字光标移动逻辑。
- 本次任务边界：
  1. 重构并升级所有行内编辑代理（Delegate）的 `eventFilter` 按键拦截：
     - `GridItemDelegate`（位于 `src/ui/ContentPanel.cpp`）
     - `ThumbnailDelegate`（位于 `src/ui/ThumbnailDelegate.cpp`）
     - `TreeItemDelegate`（位于 `src/ui/TreeItemDelegate.h`）
     - `CategoryDelegate`（位于 `src/ui/CategoryDelegate.h`）
  2. 统一拦截 `QLineEdit` 编辑器的键盘事件。针对 `Qt::Key_Up`/`Qt::Key_Down`，直接吞噬拦截；针对 `Qt::Key_Left`/`Qt::Key_Right`，当处于 `hasSelectedText()` 全选（高亮）状态时，分别将光标强制定位到 `0` 或 `text().length()` 并执行 `deselect()` 清除高亮，同时拦截该事件；处于非全选时则放行系统默认光标移动。
- 不在本次范围内的是：修改 MFT 扫描、USN 监控底座或数据库相关的非界面编辑逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-95.md

## [2026-07-26] 侧边栏分类树状展开状态重启不持久化与模型重置竞态修复

- 用户描述的现象/问题：侧边栏分类树状展开之后没有被持久化，重启主程序之后又被自动折叠了，且之前的 Plan-98 方案修复后仍未达到预期。
- 用户期望的结果：侧边栏某个分类（包括顶级托管库 ArcMeta.Library_* 及其子分类）展开之后能够完美持久化，即便重启主程序之后仍然处于展开状态。
- 本次任务边界：
  1. 修复 `CategoryPanel::initUi` 中 `modelReset` 信号响应的竞态问题，重新引入 `QTimer::singleShot(0)` 异步延迟处理，确保 `QTreeView` 已经处理完重置并渲染出物理节点后才执行展开状态的恢复。
  2. 确保 `m_isInternalUpdating` 拦截锁覆盖异步延迟期，在节点物理生成并完成展开前屏蔽所有折叠虚假信号。
  3. 完善 `saveExpandedStateToSettings` and `restoreExpandedState` 的控制与过滤。
- 不在本次范围内的是：修改 `CategoryRepo` 数据库的具体查询或系统项分类重排逻辑，修改其他面板（如内容面板）的数据加载逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-99.md

## [2026-07-26] 侧边栏分类树状展开状态重启持久化失效根治重构

- 用户描述的现象/问题：侧边栏某个分类展开之后能够持续化，重启主程序之后又被自动折叠了。
- 用户期望的结果：侧边栏某个分类展开之后能够持续化，即便重启主程序之后仍然处于展开状态，不被空状态异常覆盖，并期望添加调试日志以便追踪定位问题所在。
- 本次任务边界：
  1. 在 `modelAboutToBeReset` 信号触发瞬间将 `m_isInternalUpdating` 设为 `true`，开启拦截锁，彻底断开 `beginResetModel` -> `removeRows` 期间视图中旧节点销毁引发的高频、同步 `collapsed` 信号风暴。
  2. 在 `modelReset` 完成展开状态（`restoreExpandedState`）同步恢复后，将 `m_isInternalUpdating` 重置为 `false`，重新开放并允许用户手动交互被正常持久化。
  3. 实现析构函数 `~CategoryPanel()`，将 `m_isInternalUpdating` 设为 `true` 并安全 `disconnect` 展开/折叠信号，确保窗体销毁时无虚假状态污染配置文件。
  4. 引入全流程调试追踪日志，使用 `Logger::log` 在加载、重设、保存及析构全生命期中记录状态及防护标志的演变，供定位排查使用。
- 不在本次范围内的是：修改分类树具体的构建、重排逻辑或数据库查询。
- 对应方案文档：Modification_Plan/Modification_Plan-100.md

## [2026-07-26] 侧边栏分类树状展开状态重启持久化失效根治重构二期

- 用户描述的现象/问题：在上一轮排查中，用户发现即便重构了锁逻辑，重启后仍折叠，排查出 3 个致命死穴（类型强转失败、内存/磁盘类型混用不一致、save 里的“我的分类”字符串过激匹配拦截）。
- 用户期望的结果：通过统一加载数据类型、移除过激空拦截并实时同步 Property，以及在 `modelReset` 信号中加入兼容性安全类型解析，彻底根根治持久化。
- 本次任务边界：
  1. 统一 `loadExpandedStateFromSettings` Property 存储类型为 `QList<int>`。
  2. 重构 `saveExpandedStateToSettings`，删除包含 `"我的分类"` 的过激拦截匹配，并在物理落盘后同步向 tree property 更新最新的 `idIntList`。
  3. 在 `modelReset` 槽函数中，通过 `canConvert` 机制，增加对 `QVariantList` 与 `QList<int>` 双重类型的安全兼容解析与提取。
- 不在本次范围内的是：修改物理库构建、重排和数据库层查询。
- 对应方案文档：Modification_Plan/Modification_Plan-101.md

## [2026-07-27] 容器与状态栏 5px 隙缝悬浮进度条与状态栏计时联动落地实现

- 用户描述的现象/问题：主界面五个容器与底部状态栏之间有 5 像素 of 物理间距，现在需要在这个间距上实现一个高度为 5 像素、用于展示元数据同步进度和耗时的微型进度条，要求不破坏现有布局、无重绘抖动、在同步时显示、同步完成后自动隐藏并恢复状态栏。
- 用户期望的结果：在不改变已有布局边距的前提下，采用“无布局悬浮覆盖层 + 绝对定位”设计，在 5 像素的缝隙内精准渲染悬浮进度条，并与后台元数据同步状态服务联动，同步时显示滚动值及总耗时统计，同步完后淡出或静默隐藏并恢复状态栏。
- 本次任务边界：
  1. 在 `MainWindow.h` 中添加 `resizeEvent` 重写、悬浮计算方法 `updateProgressBarGeometry` 以及相关控制变量；
  2. 在 `MainWindow.cpp` 的 `setupSplitters` 中初始化一个高度为 5px 的不占位悬浮 `QProgressBar` 并将其设为隐藏；
  3. 实现 `resizeEvent` 与 `updateProgressBarGeometry`，通过 `centralC`、`bodyWrapper` 和 `statusBar` 的相对物理几何计算，精确确定进度条位置并调用 `raise()` 确保图层最上；
  4. 关联状态栏耗时定时器与 `SyncStatusService` 的信号。
- 不在本次范围内的是：修改任何非进度条相关的窗口层叠、对物理 MFT 扫描、磁盘监控等底座进行重构。
- 对应方案文档：Modification_Plan/Modification_Plan-105.md

## [2026-07-27] 悬浮进度条体验升级：扫描数据中语境、预计耗时 (ETA) 与从左向右推进彻底落地

- 用户描述的现象/问题：Plan-105 方案设计存在 3 点不够完美的瑕疵，即文案称谓非直接“扫描”、未对剩余任务和时间比进行精确的“预计耗时 (ETA)”推算、以及进度百分比倒退引起视觉“自右向左”的严重倒退错觉。
- 用户期望的结果：在 5px 隙缝悬浮进度条的基础上，将状态栏语境修正为“扫描数据中...”（完成时为“数据扫描完成”），将计时由静态已消耗修正为动态实时计算剩余的“预计耗时”；且锁定在任务开始时拦截并记录初始待处理项总量 `m_totalBatchCount`，用 `(总项数 - 剩余项数) / 总项数` 锁定并递增当前百分比，保证进度光条始终“由左向右”平滑推进。
- 本次任务边界：
  1. 升级 `MainWindow.h`，增设变量 `m_totalBatchCount` 保持批次总量感知；
  2. 升级 `MainWindow.cpp`，显式设定 `setInvertedAppearance(false)` 强制普通方向；
  3. 升级 `initUi()` 的 100ms 刷新槽和信号连接 logic，运用 $T_{elapsed} \times \frac{100 - P}{P}$ 动态公式推算预计耗时，并完美替换对应语境。
- 不在本次范围内的是：修改 `SyncStatusService` 本身的发射频率，或者对除了 `MainWindow`顶层控制链路之外的普通面板数据视图进行重设。
- 对应方案文档: Modification_Plan-106.md

## [2026-07-27] 创建自动导入中途取消与数据擦除机制安全落地

- 用户描述的现象/问题：在自动导入（Auto-Import）进行过程中，如果用户突然改变主意，当前缺乏一种允许用户中途停止（Stop）并彻底擦除（Erase）已导入元数据和缓存垃圾的安全撤销机制（Kill & Purge）。
- 用户期望的结果：在不破坏程序稳定性的前提下，设计并实现一套中途停止和擦除机制。用户触发取消时，首先安全、平滑地中止后台的多媒体特征提取与入库流水线（避免强杀线程造成死锁或数据库损坏），随后批量高效地删除该托管文件夹已入库的所有元数据记录，并彻底物理清理已生成的对应缩略图及宽高比高级缓存，恢复到干净的状态。
- 本次任务边界：
  1. 设计并实现 `MediaExtractorPipeline::cancelBatch()` 与 `cancelAll()` 接口，通过引入原子取消标记 `std::atomic<bool> m_isCanceled` 支持执行中断与排队任务清空，安全、非阻塞地挂起和清退流水线。
  2. 在 `MetadataManager` 中新增 `removeMetadataBatchSync` 的同步数据擦除加固方法，支持对指定托管文件夹路径进行级联批量对账和物理清退，快速擦除所有在 `metadata` 与 `category_items` 表中的对应元数据及关联记录。
  3. 提供 `CacheManager::clearCacheForPath(path)` 或 `FerrexVirtualDbModel::clearCacheForFolder(path)` 接口，在擦除数据时一并无损清理对应的缩略图、宽高比和调色板内存及磁盘高级缓存。
- 不在本次范围内的是：物理删除用户位于磁盘上的实际文件或源文件夹本身，或重构底层的 USN / MFT 核心监控及扫描时序。
- 对应方案文档：Modification_Plan/Modification_Plan-107.md

## [2026-07-28] NativeFolderWatcher 监控服务架构解耦与防抖重命名高内聚重构

- 用户描述的现象/问题：底层文件系统 IOCP 监控服务 `NativeFolderWatcher` 职责不单一，直接依赖并耦合了上层 `MetadataManager` 和 `AutoImportManager` 的具体业务操作；高密集并发/批量重命名时由于先进先出（FIFO）弹出队列极易错配；高频事件可能导致 GUI 线程在极短时间内因大量 `invokeMethod` 回调而被风暴积压、假死卡顿；另外，单例在工作线程中无意被首次加载时可能引发 QTimer 线程亲和性失效警告。
- 用户期望的结果：重构并优化 `NativeFolderWatcher` 架构，使其彻底解耦，不调用任何上层管理器，只负责基础 IO 事件处理并抛出标准 Qt 信号；重新设计防抖和并发重命名合并逻辑，杜绝 FIFO 错配并大幅削减对 GUI 线程的信号轰炸；确保单例模式下 QTimer安全初始化。
- 本次任务边界：
  1. 彻底切断 `NativeFolderWatcher` 对 `MetadataManager`、`AutoImportManager` 等上层业务类的直接头文件依赖 and 调用。
  2. 重构 `NativeFolderWatcher` 事件分发。所有捕获的文件变更（新增、修改、删除、重命名）均转化为标准的自定义结构并通过统一/分立信号（如 `fileChanged(path, action)`）分发。
  3. 优化防抖/合并算法。采用高内聚的以路径为 Key、以定时器关联的去重延迟合并缓存，削减短时间内对主线程的通知风暴；在监控层独立实现一套安全、健壮的重命名匹配配对池，不再采用基于顺序的出队机制。
  4. 解决线程亲和性警告，通过懒加载或确保 `m_debounceTimer` 正确关联主线程事件循环。
- 不在本次范围内的：修改底层 Windows ReadDirectoryChangesW/IOCP 的异步多线程核心驱动机制，或者重写 `MftReader`、数据库 SQLite 事务持久化细节。
- 对应方案文档：Modification_Plan/Modification_Plan-110.md

## [2026-07-28] 全局核心组件（ContentPanel、MainWindow 等）SRP 违规深度整改与极致重构

- 用户描述的现象/问题：整款应用中存在多处极其严重的单一职责原则（SRP）违规与职责过载（违反 SRP），例如 `ContentPanel` 混合了 UI 视图呈现、递归扫描、剪切板操作及右键菜单构建；`MainWindow` 混合了无边框拖曳、本地导航栈及预计耗时 (ETA) 业务运算；`MftReader` 混合了底层 MFT/USN 解析、系统图标提取与异步元数据填充任务调度。
- 用户期望的结果：对全款应用及核心源码文件中的 SRP 违规点进行彻底梳理和标记，制定出高度模块化、科学解耦的整体重构整改（“整改”）方案，使各组件职责彻底单一化。
- 本次任务边界：
  1. 在 `Inspect and Mark.md` 中详细标记并剖析整款应用（尤其是 `ContentPanel`、`MainWindow`、`MftReader`、`FilterPanel`）的职责过载。
  2. 规划科学的解耦重构技术路线，建立独立的服务接口（如 `ShellIconService` 负责提取图标，`NavigationHistoryService` 承接导航历史，`ContentContextMenuManager` 托管右键菜单构建等），从而彻底分流过载职责。
- 不在本次范围内的：由于目前处于只读分析师状态，不直接修改 C++ 源码文件（.cpp / .h），不破坏现行软件编译流程。
- 对应方案文档：Modification_Plan/Modification_Plan-111.md

## [2026-07-28] 修复构建配置缺失与 FilterEngine 编译类型错误

- 用户描述的现象/问题：编译 C++ 项目时，遇到了多处编译器语法错误（在 FilterEngine.cpp 中未声明的标识符 `record` / `fileName` / `pe` 等，将 `IngestedRecord` 假定为 `int` 等）以及 “util/ShellHelper.h”、“core/AppConfig.h”、“core/FileSystemService.h” 头文件无法找到的问题。
- 用户期望的结果：完全修复上述所有的 C++ 编译及头文件包含错误，使项目能够顺利通过编译。
- 本次任务边界：
  1. 修复 `src/ui/FilterEngine.h` 和 `src/ui/FilterEngine.cpp`，将未定义的 `IngestedRecord` 类型统一修改为系统实际定义的 `ItemRecord`。
  2. 修改 `CMakeLists.txt`，在 `target_include_directories` 中追加 `${CMAKE_CURRENT_SOURCE_DIR}/src` 包含路径，以解决未带 `src/` 前缀的绝对路径式头文件包含失败。
- 不在本次范围内的：新增、修改任何业务功能 and UI 样式。
- 对应方案文档：Modification_Plan/Modification_Plan-113.md

## [2026-07-28] NativeFolderWatcher 代码架构合理性、职责单一性与傻逼逻辑地毯式排查

- 用户描述的现象/问题：系统核心监控组件 `NativeFolderWatcher` 相关的代码架构、职责边界、以及事件防抖与跨线程逻辑可能存在潜在的不良设计与非单一职责设计。
- 用户期望的结果：扩大范围对 `NativeFolderWatcher` 相关的代码架构展开地毯式审计，排查是否存在设计合理性、职责单一性以及运作流程逻辑缺陷，并形成极高质量的分析和重构规划文档。
- 本次任务边界：全面审计 `src/core/NativeFolderWatcher.h` 和 `src/core/NativeFolderWatcher.cpp`，梳理其与底层 Windows IOCP API 交互、事件去重与延迟分发、重命名超时配对，以及对外部类如 `MetadataManager`、`AutoImportManager` 的硬编码耦合。由于当前角色是分析师，仅产出架构剖析与重构规划文档，不实际修改或重构任何代码文件，亦不执行编译。
- 不在本次范围内的是：物理修改代码文件、进行编译测试。
- 对应方案文档：Modification_Plan/Modification_Plan-114.md

## [2026-07-28] 主程序启动闪烁白色边框缺陷修复

- 用户描述的现象/问题：启动主程序之后，主界面的导航栏或分割器区域会短暂闪烁显示浅白色边框，随后在界面加载完成或样式表加载完毕后又消失。该白色闪烁极为影响暗色主题的视觉一致性，是不应当出现的样式瑕疵。
- 用户期望的结果：彻底根治主程序启动时的白色边框瞬时闪烁现象，使其在数据及样式未完全加载就绪的延迟空窗期前，也能保持与暗色调主题同频一致的优雅灰色/深色背景展示。
- 本次任务边界：在 `src/ui/NavPanel.cpp` 构造函数和 `m_splitter` 初始化部分中，显式设定与暗色主题匹配的背景色样式（如 `#1E1E1E`）。保证延迟异步填充数据导致的瞬时重排期间，不会露出操作系统的默认浅灰色底层边缘。
- 不在本次范围内的是：修改任何除了启动时导航栏白色边框样式修复之外的逻辑，不修改物理文件检索扫描对账逻辑，不修改数据库、MFT 扫描或其它无关的 UI 组件。
- 对应方案文档：Modification_Plan/Modification_Plan-117.md

## [2026-07-29] 屏蔽系统图标伪缩略图与普通小图标极致中央居中重构

- 用户描述的现象/问题：
  1. 标记为①的卡片里（如 `文件夹.ai`、`文件夹2.ai`）出现了原本属于系统图标自带的白色圆角边框。有的图标出现了白色边框，有的图标却未出现，严重破坏了卡片的纯暗色主题视觉感。
  2. AHK 等某些系统关联小图标（例如 `w 唯一标识符.ahk` 中的 AHK 图标）未能在卡片正中心正确绘制，而是偏移显示在了卡片的左上角，且面积比例极小，显得很不美观。
- 用户期望的结果：
  1. 彻底不显示任何白色边框的伪缩略图。
  2. 所有的图标和缩略图在卡片内部必须高品质、无偏移、比例协调地正中心绝对居中。
- 本次任务边界：
  1. 修改 `src/ui/WindowsShellThumbnailProvider.cpp`，把 Windows 原生提取缩略图的 `SIIGBF_RESIZETOFIT` 替换为 `SIIGBF_THUMBNAILONLY` 屏蔽非真正缩略图大图标，根治白色边框的系统级污染。
  2. 修改 `src/ui/CardPainterHelper.cpp`，统一重构对普通 fallback 图标在卡片里的绘制方案，强制将 icon 获取其指定合适分辨率的 pixmap，并在限定 of 圆角正中心区域进行完美的平滑拉伸绘制。
- 不在本次范围内的是：修改任何除了上述两个界面卡片细节修正以外的业务逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-121.md

## [2026-07-29] 卡片数据渲染代理多套/多种方式冗余架构审计排查

- 用户描述的现象/问题：当前应用在渲染内容区卡片数据时，可能存在多套、多种方式并存的冗余逻辑和架构硬伤。
- 用户期望的结果：对当前版本在卡片渲染层面上所有存在多套方式、职责重叠的代码设计进行全面而透彻的代码审计，并输出结构明晰的架构排查与重构整改设计。
- 本次任务边界：全面审计 `src/ui/` 目录下所有参与单元格、网格和树形卡片数据渲染的代理组件（包括 `ThumbnailDelegate`、`GridItemDelegate`、`TreeItemDelegate`），定位其在视觉物理渲染、逻辑事件过滤以及尺寸计算上的冗余与过载证据。由于当前角色是分析师，仅产出设计剖析方案，不物理修改任何功能性代码文件。
- 不在本次范围内的是：修改物理代码，引入或删除任何实际逻辑文件。
- 对应方案文档：Modification_Plan/Modification_Plan-122.md

## [2026-07-29] Library资产包重构与自动导入彻底根除 —— Modification_Plan-2.md

- 用户描述的现象/问题：用户希望对 `Modification_Plan-1.md` 进行重构，仅保留对 `ArcMeta.Library_[盘符]`（资产包物理封装与侧边栏一等公民）的重构方案，而原有的“创建自动导入”功能（涉及 CustomFolderImportDialog、监控配置 customFolders、以及底层自动导入对账等）和临时外部监控 In-Place Watcher 则须彻底弃用与物理根除。
- 用户期望的结果：彻底物理根除并弃用“创建自动导入”与 In-Place Watcher，清除一切冗余监控和对账。同时，按原版 `Modification_Plan-1.md` 重构物理层 `.arc` 封装及一等公民分类树逻辑。
- 本次任务边界：在 `Modification_Plan/` 下创建 `Modification_Plan-2.md`，精确界定删除“创建自动导入”的 UI 与底层逻辑（包括 `CustomFolderImportDialog` 声明与定义、主窗口盘符栏右键菜单、FolderButton 的监控状态解除和关联监控、`AutoImportManager` 里处理 customFolders、以及 `CoreController` 中与之关联的初始化逻辑），并完整保留一等公民 `.arc` 资产包的设计方案。
- 不在本次范围内的是：修改物理 MFT 底盘扫描驱动，或更改非自动导入相关的逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-2.md
