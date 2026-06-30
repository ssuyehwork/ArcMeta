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
