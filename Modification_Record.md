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
- **src/ui/DriveButton.h / .cpp**: [2026-11-15 14:10:05] 新增盘符状态机按钮，支持 Inactive/Active/Running/Paused 四种视觉状态及旋转动画。
- **src/ui/MainWindow.h / .cpp**: [2026-11-15 14:30:12] 注入 `DriveBar` 布局，补全展开/收起切换逻辑、物理创建 `ArcMeta.Library_盘符` 文件夹及 `AppConfig` 绑定逻辑。
- **src/core/AutoImportManager.h**: [2026-11-15 14:35:08] 补全 `startTask/pauseTask` 占位接口及 `taskFinished` 信号，对齐 UI 交互协议。
