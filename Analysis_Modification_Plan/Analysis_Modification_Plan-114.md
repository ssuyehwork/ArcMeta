# 目录导航模式下缩略图填充比例不一分析 —— Analysis_Modification_Plan-114.md

## 1. 任务背景
在“目录导航”模式（物理路径实时扫描）下，卡片显示的缩略图存在视觉不一致：部分图片能够填满整个卡片（对应用户原话：“填满了卡片”），而部分图片却以 60% 的比例缩在卡片中心（对应用户原话：“缩到了卡片中心”），导致界面参差不齐。此外，用户补充要求视频文件也应能够显示缩略图（对应补充要求：“视频文件也要显示缩略图”）。

## 2. 问题定位

### 2.1 渲染模式分支触发机制
在 `src/ui/ThumbnailDelegate.cpp` 的 `paint` 函数中，缩略图的绘制存在两个互斥分支：
1.  **覆盖分支（Fill）：** 当 `hasThumb` (`m_hasThumbnailRole`) 为 `true` 且图标不为空时，使用 `Qt::KeepAspectRatioByExpanding` 填充整个 `m.cardRect`（对应用户原话：“填满了卡片”）。
2.  **图标分支（Shrink）：** 当 `hasThumb` 为 `false` 时，系统将内容判定为“普通文件图标”，并强制将其尺寸缩小至卡片区域的 **60%** 并居中（对应用户原话：“缩到了卡片中心”）：
    ```cpp
    int iconSize = qMin(m.cardRect.width(), m.cardRect.height()) * 0.6;
    ```

### 2.2 导航模式下的判定断层（根因）
1.  **路径归一化不一致导致 Key 碰撞失败：**
    *   `MetadataManager` 内部使用全小写的路径作为缓存 Key。
    *   `ContentPanel` 在物理扫描时获取的路径可能保留了原始大小写，导致 `m_aspectRatios.contains(path)` 判定失效，使填满（对应用户原话：“填满了卡片”）的图片回退到 60% 缩小模式（对应用户原话：“缩到了卡片中心”）。
2.  **异步加载期间的判定盲区：**
    *   在异步缩略图加载完成前，`HasThumbnailRole` 恒为 `false`，导致图片初始状态均缩到中心（对应用户原话：“缩到了卡片中心”）。
3.  **视频缩略图逻辑缺失：**
    *   目前的 `isGraphicsFile` 白名单未包含视频格式，导致视频文件被视为普通文件，统一执行了 60% 缩小渲染。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 卡片上显示的缩略图有的是填满了卡片，但有的却缩到了卡片中心 | 修正 `ThumbnailDelegate` 渲染分支判定逻辑 | ✅ |
| 2    | 物理导航模式下应保持视觉一致性 | 统一路径归一化标准，确保元数据匹配一致 | ✅ |
| 3    | 视频文件也要显示缩略图 | 扩展视频格式识别并支持缩略图提取渲染 | ✅ |

## 4. 详细解决方案

### 4.1 扩展视频文件识别 (对应用户原话：“视频文件也要显示缩略图”)
修改 `src/ui/UiHelper.h` 中的 `isGraphicsFile` 或新增 `isVideoFile` 判定逻辑：
- 将常用视频格式（`mp4`, `mkv`, `avi`, `mov`, `wmv`, `flv`, `webm`）纳入识别范围。
- 确保 `getShellThumbnail` 能够正确提取视频文件的第一帧或系统生成的视频预览图。

### 4.2 统一物理路径归一化准则
在 `src/ui/ContentPanel.cpp` 和 `src/ui/ContentPanel.h` 中，确保所有物理路径在进入 Model 缓存之前，均调用 `MetadataManager::normalizePath`。
- 修改 `ContentPanel::createItemRecord`：对传入的路径执行归一化。

### 4.3 优化 HasThumbnailRole 判定逻辑
修改 `src/ui/ContentPanel.cpp` 中 `FerrexVirtualDbModel::data` 对 `HasThumbnailRole` 的返回逻辑：
- **逻辑重构：** 只要文件后缀属于 `UiHelper::isGraphicsFile` 或视频格式范畴（对应用户原话：“视频文件也要显示缩略图”），无论其 `width/height` 是否就绪，`HasThumbnailRole` 均应返回 `true`。
- **目的：** 强制 `ThumbnailDelegate` 即使在加载占位期间也预设为填充（对应用户原话：“填满了卡片”）模式分支。

### 4.4 修正 ThumbnailDelegate 渲染策略
修改 `src/ui/ThumbnailDelegate.cpp`：
- 若项类型为“图片”或“视频”（对应用户原话：“视频文件也要显示缩略图”），即使缩略图尚未从异步线程返回，也应准备好以全尺寸（对应用户原话：“填满了卡片”）绘制背景或占位图。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/ThumbnailDelegate.cpp`: 修改渲染分支判定。
- [ ] `src/ui/ContentPanel.cpp`: 修正 `FerrexVirtualDbModel` 数据提供逻辑。
- [ ] `src/ui/UiHelper.h`: 扩展支持视频格式。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MetadataManager` 的核心数据库存储逻辑。

## 6. 实现准则与预警【核心】

1.  **视频缩略图效能：** 提取视频缩略图较图像更耗时，必须确保 `getShellThumbnail` 的异步磁盘缓存机制（v14 标识）能有效工作，防止界面滚动卡顿。
2.  **视觉占位：** 视频文件在缩略图加载期间也应使用灰度占位背景（`#3A3A3A`），确保 Cover 区域被填满（对应用户原话：“填满了卡片”）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 路径归一化 | 统一转换为全小写以确保内存缓存 Key 匹配一致性 | ✅ 符合 |
| UI 渲染 | 保持绝对一致，严禁新建同类组件 | ✅ 符合 |
| 异步处理 | 耗时加载需在锁外执行，防止 UI 挂起 | ✅ 符合 |

## 8. 待确认事项
- 暂无。
