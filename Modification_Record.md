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
- [2026-06-29 15:16:16] **src/ui/MainWindow.h / .cpp**:
    - 标题栏新增 `m_btnToggleDriveBar` 按钮用于控制盘符栏显隐。
    - 集成 `m_driveBarWidget` 并实现 `initDriveBar` 自动扫描系统盘符生成按钮组。
    - `onDriveButtonClicked` 设为 TODO 占位，等待后期业务安排。
