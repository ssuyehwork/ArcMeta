# Analysis and Modification Plan - UI 框架排查与 FramelessDialog 深度分析

## 1. UI 界面体系排查现状
经过对 `src/ui` 目录的全面扫描，识别出项目中“自定义无边框框架”与“系统原生框架”的分布如下：

### 1.1 自定义无边框窗口 (Custom UI)
目前核心交互界面已基本实现无边框化，由程序接管绘制与交互逻辑：
- **MainWindow (主窗口)**：完全自定义，带边缘缩放功能。
- **FramelessDialog 族群**：
    - `ProgressDialog` (进度条)
    - `BatchProgressDialog` (批处理进度)
    - `BatchRenameDialog` (批量重命名)
    - `BatchRenamePreviewDialog` (重命名预览)
    - `CategorySetPasswordDialog` (分类加密)
    - `CategoryLockDialog` (分类解锁)
    - `FramelessInputDialog` (通用输入)
    - `FramelessConfirmDialog` (通用确认)
- **浮动/悬停窗口**：
    - `LoadingWindow` (加载动画)
    - `ToolTipOverlay` (自定义气泡)
    - `QuickLookWindow` (空格预览)
    - `AddressHistoryPanel` / `SearchHistoryPanel` (历史面板)

### 1.2 原生框架残留 (Native UI)
以下组件仍调用操作系统标准窗口，导致视觉体验在关键节点产生断裂：
- **QMessageBox**：用于错误警告、删除确认等（分布于 `TagManagerView`, `ContentPanel`, `ImportHelper` 等）。
- **QFileDialog**：用于目录选择（在 `BatchRenameDialog` 中使用）。

## 2. FramelessDialog 逻辑架构分析
`FramelessDialog` 是对话框体系的核心，其实现逻辑具有高度的工业特征：

### 2.1 视觉实现
- **无边框标志**：构造时使用 `Qt::FramelessWindowHint | Qt::Window`。
- **Win11 原生圆角**：通过 `DwmSetWindowAttribute` 调用 DWM 引擎，实现系统级抗锯齿圆角（DWMWCP_ROUND），而非简单的 QSS 切圆角。
- **三层容器架构**：
    - `OuterLayout`: 顶层布局，边距为 0。
    - `DialogContainer`: 核心容器，应用了 `#2E2E2E` 背景和 `6px` 圆角。
    - `MainLayout`: 垂直分层，顺序为：`TitleBar` -> `4px Spacing` -> `1px Separator Line` -> `ContentArea`。

### 2.2 交互逻辑
- **窗口拖拽**：重写 `mousePressEvent`。逻辑：判断点击位置是否在 `TitleBar` 区域，且避开了 `QPushButton` 子控件，若满足则开启 `m_isDragging` 追踪。
- **关闭交互**：重写 `keyPressEvent (Escape)`。逻辑：优先执行“输入框清空”动作（两段式 UX），再次按下才执行 `reject()`。
- **置顶管理**：内置 `m_pinBtn` 切换 `Qt::WindowStaysOnTopHint`。

## 3. 待申报/改进建议 (Declaration Candidates)
基于上述排查，以下点建议作为 `Declaration_Log.md` 的潜在申报项：
- **边缘缩放缺失**：`FramelessDialog` 目前不支持通过鼠标边缘拉伸调整尺寸（不同于 `MainWindow`），导致固定比例下某些内容显示受限。
- **QMessageBox 视觉污染**：建议后续建立 `FramelessMessageBox` 静态接口，封装 `FramelessConfirmDialog` 以彻底替换原生弹窗。
- **QFileDialog 风格化**：原生文件对话框在暗色模式下对比度极差，建议后续研发自定义的文件/文件夹浏览器。

## 4. 执行规范提醒
在后续根据此方案进行任何代码层面的标准化重构时，必须严格遵守以下原则：
1. **严禁脑补与顺手修改**：仅限处理本方案涉及的 UI 对齐或逻辑重构。
2. **发现问题即申报**：如发现 `FramelessDialog` 内部存在未记录的 Bug（如 DWM 句柄泄漏风险），必须记录至 `Declaration_Log.md`，不得私自修复。
