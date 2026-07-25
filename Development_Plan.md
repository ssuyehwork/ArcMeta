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

- 用户描述的现象/问题：当前版本中的三种视图（列表、网格、合理自适应对齐模式）存在逻辑冗余、职责过载以及交互/渲染卡顿等傻逼逻辑架构问题。
- 用户期望的结果：对三种视图底层控制链展开深度排查，找出不合理的架构缺陷并形成极致解耦与性能提升的优化方案，并产出详细设计。
- 本次任务边界：审计并理清 `JustifiedView`、`ListResultView`、`GridResultView`、`JustifiedResultView` 以及 `ContentPanel` 之间在视图控制、数据映射 and 布局渲染上的依赖关系，设计职责更清晰、耦合度更低的模块化结构。
- 不在本次范围内的是：实际修改或编译代码，移植非视图类的其他外部功能。
- 对应方案文档：Modification_Plan-35.md

## [2026-07-20] 彻底放弃当前 QuickLookWindow 运行逻辑并使用 FERREX-META 逻辑替代重构

- 用户描述的现象/问题：当前版本的 `QuickLookWindow` 预览运行逻辑不符合预期，其处理逻辑 and 界面交互（如图像缩放、音视频媒体处理、文本二进制检测及编码识别等）功能不够完备。
- 用户期望的结果：彻底放弃当前版本的 `QuickLookWindow` 运行逻辑，将其替换为 `FERREX-META` 版本中的 `QuickLookWindow` 运行逻辑。这包括采用专门设计的 `QuickLookGraphicsView`（支持滚轮缩放、双击恢复/适配大小、鼠标拖拽移动及光标跟随变动）、引入更完整的多媒体及文本二进制与编码自动检测识别渲染，实现更好的大文件和特殊文件类型的预览。
- 本次任务边界：彻底重构 `src/ui/QuickLookWindow.h` and `src/ui/QuickLookWindow.cpp`，将 `FERREX-META` 版本的 `QuickLookWindow` and `QuickLookGraphicsView` 控制与交互机制移植并合并至其中，适配 `ArcMeta` 命名空间及符合本项目标准的样式设计。同时确保当前项目正在运行的核心快捷键/信号链路继续工作（评分打标 `ratingRequested`、颜色标签打标 `colorRequested` 以及切图 `prevRequested`/`nextRequested` 信号），并对音视频进行优雅的占位降级兼容。
- 不在本次范围内的是：修改 `MainWindow` 的主导航调度机制 or 任何非预览关联的面板组件。
- 对应方案文档：Modification_Plan-36.md

## [2026-07-21] 预览界面滚动条样式对齐考古规范优化

- 用户描述的现象/问题：按下空格键打开预览界面后的滚动条样式，违背了考古，由上一代 AI 导致。
- 用户期望的结果：将预览界面（QuickLookWindow）内的滚动条样式彻底重构，使其严格遵循全局规范：宽度 10px、圆角 3px、背景透明、Handle 颜色对齐 `BorderColor` #333333。
- 本次任务边界：修改 `src/ui/QuickLookWindow.cpp` 中 `QuickLookGraphicsView` 和 `QPlainTextEdit` 控件的水平及垂直滚动条的 QSS 样式。
- 不在本次范围内的是：修改预览界面其他的图片/文本渲染、缩放或事件分流逻辑。
- 对应方案文档：Modification_Plan-37.md

## [2026-07-21] 预览界面多媒体残留代码彻底物理根除与净化

- 用户描述的现象/问题：上一代 AI 在收到彻底根除多媒体播放功能的指令后，依然在 `QuickLookWindow` 中保留了多媒体播放及占位相关冗余代码。
- 用户期望的结果：彻底删除并根除 `QuickLookWindow` 中的所有多媒体组件（如播放、暂停、进度条、多媒体相关容器控件和占位框等）和无用快捷键与接口，音视频转走标准系统大图标降级展示。
- 本次任务边界：彻底删除 `src/ui/QuickLookWindow.h` 与 `src/ui/QuickLookWindow.cpp` 中的所有多媒体成员变量、宏定义、多媒体容器构建、以及 `renderMedia`、`resetMedia`、键盘 `P` 按键处理逻辑等。
- 不在本次范围内的是：修改预览界面其他的图片/文本渲染、缩放或事件分流逻辑。
- 对应方案文档：Modification_Plan-38.md

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
- 对应方案文档：Modification_Plan-39.md

## [2026-07-21] ThumbnailDelegate 职责过载审计与模块化拆分规划

- 用户描述的现象/问题：单元格展示委托 `ThumbnailDelegate` 存在多维重绘、生命期、排版 and 提示的职责过载，难以维护及复用。
- 用户期望的结果：对其进行代码职责过载分析并输出模块化的单一职责拆解和落地规划。
- 本次任务边界：对 `src/ui/ThumbnailDelegate.h` 与 `src/ui/ThumbnailDelegate.cpp` 展开整体架构梳理，定位其过载现状，设计面向高内聚、零开销、低耦合的模块化（视觉物理渲染、文本排版、控制器生命期分流）拆分图纸。由于本 Turn 为只读分析师模式，不修改任何项目代码文件。
- 不在本次范围内的是：修改逻辑代码、在外部执行构建验证。
- 对应方案文档：Modification_Plan-40.md

## [2026-07-22] 视图按钮及缩放滑杆功能移植重构

- 用户描述的现象/问题：当前版本缺少直观的“缩放滑杆”和“视图模式一键切换”按钮，且 Ctrl+滚轮 缩放无法动态调整视图卡片的大小，交互体验滞后。
- 用户期望的结果：将 FERREX-META 版本的滑杆（m_sizeSlider）和排列方式视图按钮（viewBtn）移植到当前版本并关联使用，同时支持通过 Ctrl+滚轮 在内容视图上进行自由的比例缩放。
- 本次任务边界：
  1. 在主窗口 `MainWindow::setupCustomTitleBarButtons()` 对应的自定义按钮组中，植入“缩放滑杆”和“视图模式一键切换按钮”，调整按钮间距物理对齐全局规范。
  2. 重构 `ContentPanel` 的 `wheelEvent`，捕获 Ctrl+滚轮 动作实现滑杆尺寸/内容卡片尺寸 `m_zoomLevel`（限制在 96~128px 之间）的物理同频缩放，并保证滑杆值及 `updateGridSize()` 持久化一致。
  3. 在“视图按钮”下拉菜单中，完美对接 `ListView`、`GridView` (网格自适应即 JustifiedMode) 以及 `JustifiedViewMode` 的逻辑切换。
- 不在本次范围内的是：重写或修改物理 USN 监控底座或底层 MFT 文件过滤搜索流程。
- 对应方案文档：Modification_Plan-45.md

## [2026-07-24] 全款应用架构与文件职责过载深度排查

- 用户描述的现象/问题：应用中可能存在整体混乱的逻辑架构，文件职责过载（违反SRP），以及存在某些功能具有不合理的傻逼逻辑架构。
- 用户期望的结果：对全款应用及核心源码文件进行地毯式的通读与细粒度审计，标记所有存在职责过载（违反 SRP）的文件，定位逻辑架构设计不合理的傻逼逻辑架构功能。
- 本次任务边界：对 `src/` 目录下的核心逻辑、业务模块 and UI 骨架（如 `ContentPanel`, `MainWindow`, `MetadataManager`, `MftReader` 等）及各组件数据流向、跨层耦合和 UI 层业务硬编码进行地毯式地深层审计与排查，输出架构漏洞、混乱点、文件职责过载证据（行号 + 代码片段）并归档。
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
  2. 在 `CategoryPanel::onSetColor` 和 `onRandomColor` 中增加同步写入，当分类绑定的物理文件夹非空时，同步调用 `MetadataManager::instance().setColor` 将新颜色同步写入对应的物理文件夹缓存与持久化中。
  3. 确保该双向同步动作仅对已入库的物理文件夹/分类有效，不影响其他普通未入库项目。
- 不在本次范围内的是：修改非颜色属性的元数据持久化、或者对非入库文件更改颜色的独立打标逻辑。
- 对应方案文档：Modification_Plan/Modification_Plan-61.md

## [2026-07-25] 彻底根除随机/设置颜色旧UI与在右键菜单上直接以悬停白圈色块展示并同步存库

- 用户描述的现象/问题：现有的右键菜单及部分分类/文件夹设置中采用“随机颜色”和“设置颜色”的传统菜单项（QAction）及弹窗操作链路复杂、不够直观。
- 用户期望的结果：彻底根除“随机颜色”和“设置颜色”相关逻辑代码，不可保留。直接将当前现有的标注色直接显示在右键菜单上作为一排水平排列的色块进行选择，悬停在某个色块上方时通过白圈突显色块。选择色块后，将色值同时存入到 categories 表的 color 字段和 metadata 表的 color 字段中。
- 本次任务边界：
  1. 彻底删除 `CategoryPanel` 及 `MainWindow`（FolderButton 菜单）中的“随机颜色”与“设置颜色”旧 QAction 和旧对应响应函数（如 `onRandomColor` / `onSetColor`）。
  2. 针对 `CategoryPanel` 侧边分类右键菜单、`MainWindow` 中的 FolderButton 右键菜单，以及 `ContentPanel` 内容右键菜单中的“设定颜色标签”（ActionColorTag）子菜单，实现全新的一排水平排列色块快捷操作区域（可以使用自定义 `QWidgetAction` 加载色块容器）。
  3. 每个色块尺寸和间距根据界面情况合理设定（如圆形色块，直径 20px-24px，间隔 6px-8px），且鼠标悬停在色块上方时绘制出明晰的白色同心圆突显色块边缘。
  4. 选中某个色块后，立即调用更新接口：
     - 如果是在 `CategoryPanel` 对分类进行设置，将色值写入对应分类在 `categories` 表的 `color` 字段，同时如果该分类绑定了物理文件夹路径，则将对应路径在 `metadata` 表的 `color` 字段也一并同步更新；
     - 如果是在 `MainWindow` 对 FolderButton 自定义 monitored folder 进行操作，将色值写入 AppConfig 中对应属性，并更新对应物理路径在 `metadata` 表的 `color` 字段（如果已登记到库中，也会同步到绑定映射分类）；
     - 如果是在 `ContentPanel` 对登记的项目进行操作，将色值写入该项目在 `metadata` 表的 `color` 字段，若该项目是已入库且绑定了侧边栏映射分类的文件夹，也将色值存入对应分类的 `categories` 表的 `color` 字段中。
- 不在本次范围内的是：修改非颜色属性的其他关联持久化，或者调整与色块绘制无关的其他右键菜单项。
- 对应方案文档：Modification_Plan/Modification_Plan-62.md
