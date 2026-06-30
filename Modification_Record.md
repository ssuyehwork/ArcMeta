# 修改记录 (Modification Record)

## [2024-11-20]
### 缩略图填充比例不一分析与视频缩略图支持 (Plan-114)
- **src/ui/UiHelper.h**: 在 `isGraphicsFile` 中扩展了视频格式支持 (`mp4`, `mkv`, `avi`, `mov`, `wmv`, `flv`, `webm`)。
- **src/ui/ContentPanel.cpp**: 
    - `createItemRecord` 引入 `MetadataManager::normalizePath` 确保物理扫描路径与元数据 Key 归一化一致。
    - `FerrexVirtualDbModel::data` 的 `HasThumbnailRole` 为视频/图形文件预设返回 `true`。
- **src/ui/ThumbnailDelegate.cpp**: 
    - 优化 `paint` 逻辑，即使缩略图异步加载中，图形/视频文件也强制使用填充模式分支。
    - 引入 `#3A3A3A` 灰色占位背景，消除从“60% 缩小图标”到“全屏缩略图”的视觉闪烁。

### 盘符工具栏模块移植 (Plan-115)
- [2026-06-29 15:16:16] **src/ui/DriveButton.h / .cpp**: 从旧版本还原盘符状态机按钮组件，支持 Inactive/Active/Running/Paused 四种视觉状态及旋转动画。
- [2026-06-29 16:38:19] **src/ui/MainWindow.h / .cpp**: 
    - 物理加固标题栏按钮组，找回缺失的 `m_btnToggleDriveBar` 切换按钮（当前共 8 个按钮）。
    - 严格遵循全局视觉规范，在标题栏、地址栏、盘符栏与主体区域间添加 `addSpacing(5)` 物理间距。
    - 升级 `m_driveBarWidget` 样式，实现上下双 1px 切割线（`border-top` & `border-bottom`）。
    - 集成 `m_driveBarWidget` 并实现 `initDriveBar` 自动扫描系统盘符生成按钮组。

### 唯一入库路径重构与有序动态迁移 (Plan-116)
- [2026-06-30 10:20:00] **src/meta/MetadataManager.h / .cpp**:
    - `persistAsync` 增加 `authorized` 参数，非 USN Journal 触发的 INSERT 操作将被拦截，确保入库路径唯一。
    - 修复 `persistAsync` 中 `isNew` 校验时的 SQL 绑定缺失问题。
    - 同步更新 `registerItem` 和 `registerItemsAsync` 接口签名。
- [2026-06-30 10:45:00] **src/util/ImportHelper.h / .cpp**:
    - 重构 `importPaths` 为纯物理迁移模式，移除主动数据库登记，依靠 USN 异步补完入库。
- [2026-06-30 11:15:00] **src/ui/ContentPanel.cpp**:
    - 实现视图级编辑权限拦截：物理导航模式下禁止对库外项目进行元数据编辑。
    - 物理导航进入托管库时自动重定向至镜像加载模式，实现加速渲染。
    - 重构右键菜单，实现基于“库根目录置顶 + atime 排序”的有序动态迁移菜单。
- [2026-06-30 11:30:00] **src/ui/CategoryPanel.cpp**: 
    - 同步重构拖拽导入逻辑，使其符合物理迁移准则。
- [2026-06-30 11:40:00] **AGENTS.md**:
    - 固化 Plan-116 核心红线：唯一入库路径、导航加速加载及有序迁移规范。
- [2026-06-30 16:30:00] **src/ui/MainWindow.cpp / MetaPanel.cpp / ContentPanel.cpp**:
    - 彻底废除 UI 链路中所有对 `registerItem` 的主动调用，严格遵循“入库路径唯一化”红线。

### 右键菜单语义分流重构 (Plan-117)
- [2026-06-30 14:20:00] **src/meta/MetadataManager.h / .cpp**:
    - 实现 `isInsideManagedLibrary` 静态接口，封装高性能物理路径归属判定。
- [2026-06-30 15:10:00] **src/ui/ContentPanel.cpp**:
    - 重构右键菜单逻辑：基于 `isMirrorSource` 判定实现“归类到...”与“迁移”的绝对语义互斥。
    - 将所有元数据标记类操作（星级、颜色、置顶、解析颜色）移入镜像源分支，实现库外 UI 锁定与库内操作引导。
    - 废除过时的“收揽入库”术语，统一使用“解析颜色/重新解析颜色”。

### 搜索框与筛选面板集成架构重构 (Plan-118)
- [2026-06-30 16:50:00] **src/ui/ContentPanel.cpp**:
    - 重构 `search` 函数：废除独立的 `"search"` 视图模式，搜索关键词直接驱动本地 `FilterState` 过滤引擎，确保视图上下文（分类/导航）一致性。
- [2026-06-30 17:15:00] **src/ui/FilterPanel.h / .cpp**:
    - 实现数据源感知逻辑 `setMirrorSource`：当处于物理磁盘源（库外导航）时，自动隐藏评级、颜色、备注等依赖元数据库的筛选分组。
- [2026-06-30 17:30:00] **src/ui/MainWindow.cpp**:
    - 统一 `doSearch` 调用链：由 `ContentPanel::search` 统领过滤逻辑。
    - 在 `unifiedNavigateTo` 导航中枢中注入数据源感知通知，确保筛选面板与内容源状态同步。

### getManagedFolderAbsolutePath 默认兜底恢复
- [2026-06-30 18:20:00] **src/core/AutoImportManager.cpp**:
    - 为 `getManagedFolderAbsolutePath` 增加约定优于配置的默认兜底逻辑：若数据库无显式配置，则默认探测并关联 `ArcMeta.Library_[盘符]` 文件夹。

### 最近访问记录与 USN 信号补全 (Plan-119 & 120)
- [2026-07-01 10:30:00] **src/core/AutoImportManager.h / .cpp**:
    - 实现 `recordRecentVisitedFolder` 与 `getRecentVisitedFolders` 接口，支持基于磁盘卷序列号的物理文件夹访问历史记忆（上限 14 条）。
    - 补全 `entryUpdated` 信号监听，确保通过 Move（改名）操作移入托管库的项目能被 USN 感知并触发自动入库。
- [2026-07-01 11:00:00] **src/ui/ContentPanel.cpp**:
    - 在 `loadDirectory` 导航触发点注入历史记录调用。
    - 重构右键“迁移”菜单，将“迁移至最近活跃位置”替换为真实的历史路径列表，提供快捷物理位移入口。
    - 补全 `../core/AutoImportManager.h` 引用，解决静态方法调用导致的编译错误。

### 自动入库逻辑临时诊断日志补丁
- [2026-07-01 15:30:00] **src/core/AutoImportManager.cpp**:
    - 在 `startListening`、`onEntryAdded`、`onEntryUpdated` 及 `getManagedLibraryPath` 中追加 `[DIAG]` 前缀的调试日志，用于排查自动入库触发断点。

### 最终诊断：验证 startListening 调用是否被实际执行 (Plan-124)
- [2026-07-01 16:00:00] **src/main.cpp**:
    - 在调用 `startListening` 前后注入 `[DIAG-MAIN]` 日志，并输出 `AutoImportManager` 实例地址以排查单例状态。
- [2026-07-01 16:00:00] **src/core/AutoImportManager.cpp**:
    - 在 `startListening` 函数入口（if 判断前）注入 `[DIAG]` 日志，记录 `m_isListening` 的真实初值。

### 统一库路径计算逻辑 (Plan-121)
- [2026-07-01 14:00:00] **src/core/AutoImportManager.h / .cpp**:
    - 将 `getManagedFolderAbsolutePath` 重命名并公开为 `static getManagedLibraryPath`，支持自动解析路径所属卷序列号并应用默认兜底逻辑。
- [2026-07-01 14:30:00] **src/ui/ContentPanel.cpp**:
    - 在 `onCustomContextMenuRequested` 中，重构“迁移”子菜单构建逻辑：复用 `getManagedLibraryPath` 计算 Library 根目录。若目标盘尚未创建库，则显式提示“该盘库存未创建”并禁用相关操作。

### 冗余监控日志与闲置对账逻辑移除
- [2026-06-30 10:07:34] **src/ui/MainWindow.h / .cpp**:
    - 彻底移除 `[MONITOR]` 资源监控日志及相关 `m_resourceMonitorTimer` 逻辑。
    - 彻底移除“检测到系统闲置超过30秒，触发自动对账同步...”逻辑代码及相关 `m_idleTimer` 成员与事件过滤逻辑，不再保留闲置对账功能。

### CategoryPanel 调试日志清理
- [2026-06-30 10:18:39] **src/ui/CategoryPanel.cpp**:
    - 彻底移除所有带有 `[CategoryPanel]` 前缀的调试日志打印语句，优化生产环境日志输出。

### 事件过滤器安装逻辑修复
- [2026-06-30 10:21:37] **src/ui/MainWindow.cpp**:
    - 修复因移除闲置检测逻辑误删 `installEventFilter(this)` 导致搜索历史等 UI 交互失效的问题。
