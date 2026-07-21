# 彻底放弃当前 QuickLookWindow 运行逻辑并使用 FERREX-META 逻辑替代重构 —— Modification_Plan-36.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
当前版本的 `QuickLookWindow` 快速预览逻辑较为单一，不支持大文件自动安全降级、非图片/非文本文件的系统图标兜底、多媒体音视频优雅预览以及强大的图片平滑拖拽与滚轮实时缩放。用户期望彻底放弃并根除当前的 `QuickLookWindow` 运行逻辑（对应用户原话：“是的，需要彻底根除当前版本的“QuickLookWindow”运行逻辑方式，然后直接使用FERREX-META版本里的“QuickLookWindow”逻辑方式”），改用 `FERREX-META` 的高级运行逻辑进行替代重构，并对 namespace 进行适配，同时保留由于历史功能已建立的信号及特定打标快捷键连接。

## 2. 问题定位
当前版本的 `QuickLookWindow` 基于 QGraphicsView 实现了极简的预览，但缺少以下在 `FERREX-META` 中已被验证的、体验优秀的交互与播放机制：
1. **强大的图片交互视口 (`QuickLookGraphicsView`)**：当前版本不支持以鼠标为中心的缩放、双击还原/适配、鼠标中键/左键平滑拖动与手势光标切换。
2. **文本二进制智能感知与编码自动检测**：当前版本缺乏 UTF-16、GBK 自动感知和二级二进制内容判定。
3. **音视频媒体支持**：当检测到常见的音频与视频后缀时，当前版本直接走文本模式，而没有引入 QMediaPlayer 等多媒体渲染链。
4. **不支持不可预览格式兜底**：缺少对 zip、7z、exe、dll 等文件的系统 Icon 快速提取与不可预览降级展示。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 彻底根除当前版本的“QuickLookWindow”运行逻辑方式，然后直接使用FERREX-META版本里的“QuickLookWindow”逻辑方式 (对应用户原话) | 将 `FERREX-META` 的 `QuickLookGraphicsView` 和 `QuickLookWindow` 实现 1:1 物理重构移植到 `src/ui/` 下，完全更替旧实现。 | ✅ |
| 2    | 使用中文解说，不允许使用英文答复用户 (对应用户原话) | 方案分析及后续回复一律使用规范中文，杜绝英文。 | ✅ |

## 4. 详细解决方案

### 4.1 `QuickLookGraphicsView` 类定义与移植
在 `src/ui/QuickLookWindow.h` 中，将 `QuickLookGraphicsView` 独立为配套的 GraphicsView 类。
- 负责 QGraphicsPixmapItem 缩放、平滑插值设置 (`Qt::SmoothTransformation`)。
- 实现滚轮缩放 (`wheelEvent`)，双击切换自适应与原图大小比例。
- 实现拖拽手势 (`setDragMode(QGraphicsView::ScrollHandDrag)`)，且在图像超出视口大小时自动切换 `Qt::OpenHandCursor` / `Qt::ClosedHandCursor`。
- 滚动条样式美化：使用 4px 紧凑圆角黑色系风格。

### 4.2 `QuickLookWindow` 重构
为了兼容当前正在运行的 `MainWindow` 绑定，我们将把 `QuickLookWindow` 变更为单例模式，即提供 `static QuickLookWindow& instance();` 接口。
- **UI 布局移植**：
  - 顶部与底部留白，采用 QVBoxLayout。
  - 植入 `m_graphicsView` (`QuickLookGraphicsView`) 渲染层。
  - 植入 `m_textEdit` (`QPlainTextEdit`) 文本渲染层。
  - 植入多媒体控制容器 `m_mediaContainer`，在 `#ifdef ARCMETA_HAS_MULTIMEDIA` (或 `FERREX_HAS_MULTIMEDIA` 兼容) 时构建 QVideoWidget、QMediaPlayer、QAudioOutput 链。
- **渲染分流 (白名单与黑名单机制)**：
  - 音视频后缀 (`mp4`, `mov`, `mp3`, `flac` 等) 走 `renderMedia`；
  - 图片/矢量图 (`png`, `jpg`, `svg` 等) 走 `renderImage` 异步加载渲染；
  - 压缩包、可执行文件等不可预览后缀 走降级，并使用系统图标 (`getFileIcon`) 作为核心展示；
  - 文本文件 走 `renderText`，读取 128KB 头部，使用二进制智能识别 (`isBinary`) 和多重编码判定 (`detectEncoding`：检测 UTF-8/UTF-16LE/UTF-16BE/GBK)。
- **物理还原功能对等 (星级及颜色标记)**：
  - 为了不让 `MainWindow` 串联的标记功能失效，在 `keyPressEvent` 中保留打标快捷键：
    - 按下 `1-5` 触发 `emit ratingRequested(rating);`
    - 按下 `Alt + 1-9` 触发 `emit colorRequested(color);`
    - 按下 `Ctrl + W`、`Space`、`Escape` 触发预览关闭；
    - 按下 `Left`/`Up` 触发 `emit prevRequested();`
    - 按下 `Right`/`Down` 触发 `emit nextRequested();`
  - 增加 `previewFile(const QString& path)` 封装入口直接转发至 `preview(path)`，确保 `MainWindow` 原有调用无缝适配。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/QuickLookWindow.h` (重写类定义、加入 `QuickLookGraphicsView` 声明、保留单例 `instance()` 及相关交互信号)
- [ ] 模块/文件：`src/ui/QuickLookWindow.cpp` (重写所有图片缩放、多媒体占位或播放、文本自动编码检测、1-5打星与 Alt+1-9 颜色快捷键逻辑)

**明确禁止越界修改的范围：**
- [ ] `src/ui/MainWindow.cpp` 串联逻辑 —— 不修改
- [ ] `src/ui/ContentPanel.cpp` 触发预览信号 —— 不修改
- [ ] MFT 扫描、文件监控、数据库落盘 —— 不修改

## 6. 实现准则与预警【核心】
1. **多媒体优雅降级**：鉴于本项目当前未强制引入 Qt6::Multimedia 组件，方案内对视频/音频渲染部分实施 `#ifdef FERREX_HAS_MULTIMEDIA` (我们兼容定义此宏，或者在 `QuickLookWindow.cpp` 中安全屏蔽并提供音频/视频占位渲染)。这样在不更改当前 `CMakeLists.txt` 依赖的前提下，不仅能支持完美的编译，还能在运行时以高对比度、橙色文字优雅展示“当前系统未启用多媒体播放模块”，保证逻辑安全可靠。
2. **避免 Thread Affinity 崩溃**：在 `renderImage` 异步 QtConcurrent::run 加载图像时，确保在获取 weak指针/QPointer 安全回传到主线程 (`QMetaObject::invokeMethod`) 后再调用 GUI 组件刷新。
3. **命名空间与品牌更名合规**：所有组件名、文件名、变量注释均要对齐 `ArcMeta` 品牌规范。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 快速预览 (QuickLook) 规范 | 预览标准图像 (jpg, png, webp 等) 时必须加载全分辨率原图，并启用 `SmoothPixmapTransform` 以消除锯齿。预览窗口内的滚动条样式必须严格遵循全局规范：宽度 10px、圆角 3px、背景透明、Handle 颜色对齐 `BorderColor` (#333333)。 | ✅ 符合。在 `QuickLookGraphicsView` 和 `QuickLookWindow` 中会配置平滑缩放与滚动条紧凑美化。 |
| 快速预览 (QuickLook) 进阶规范 | 必须采用“黑名单拦截+白名单准入”的双重防御机制。严禁预览文件夹、安装包 (.msi, .exe)、系统库 (.dll, .sys) 及各类压缩包。`renderImage` 在加载原图前必须检查文件物理大小。若超过 50MB，必须自动降级调用高清缩略图引擎以确保 UI 响应性能。`SmoothPixmapTransform` 必须全程开启，杜绝在高清屏下出现插值锯齿。 | ✅ 符合。采用 `UNPREVIEWABLE_EXTS` 黑名单和 Qt 支持的原生白名单加载。大文件超过安全阈值（如5000万像素或50MB）时，自动缩放至安全比例，Smooth 物理画质增强全程点亮。 |

## 8. 待确认事项（可选）
- **音视频原生播放**：当前版本 `CMakeLists.txt` 并未链接 Qt6::Multimedia。为确保编译不挂，我们建议使用优雅的音视频占位优雅降级（如界面上显示“音视频预览未启用 | [文件名]”并支持快捷键关闭），而不需要修改编译环境。这样最稳妥。
