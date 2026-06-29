# 盘符管理栏 UI 复刻方案 —— Analysis_Modification_Plan-115.md

## 1. 任务背景
根据用户要求，需要将“旧版本-8”中的盘符管理功能复刻到当前版本中。该功能包含：
1. 位于标题栏右侧的展开/收起切换按钮（对应 `image.png` 圈选部分1）。
2. 位于地址栏下方的盘符快捷管理栏（对应 `image.png` 圈选部分2）。
本次任务仅要求实现 UI 部分的复刻，核心业务逻辑（如任务启动、状态同步）暂时设为 TODO。

## 2. 问题定位与考古
- **核心组件**：由 `src/ui/DriveButton.cpp` 和 `DriveButton.h` 实现单盘符视觉反馈（包含 inactive/active/running/paused 四态）。
- **布局逻辑**：在 `MainWindow::initUi` 中调用 `initDriveBar()` 初始化容器，并在 `setupCustomTitleBarButtons()` 中注入 `m_btnToggleDriveBar`。
- **资源支持**：依赖 `SvgIcons.h` 中的 `chevrons_down` 和 `chevrons_up` 矢量定义。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将图中红色圈着的这部分（1号）从“旧版本-8”里复刻 | 在标题栏右侧添加 `chevrons_down` 切换按钮 | ✅ |
| 2    | 将图中红色圈着的这部分（2号）从“旧版本-8”里复刻 | 在地址栏下方添加盘符快捷管理栏容器 | ✅ |
| 3    | 只需要解决UI部分即可，其他部分暂时设为TODO | 移植 `DriveButton` 样式逻辑，点击槽函数设为 `TODO` | ✅ |

## 4. 详细解决方案

### 4.1 移植核心组件 DriveButton
- **创建文件**：在 `src/ui/` 下新建 `DriveButton.h` 和 `DriveButton.cpp`。
- **UI 对齐**：从“旧版本-8”复刻 `paintEvent` 逻辑。
    - **Inactive 状态**：使用 `#333333` 背景，灰色文字。
    - **Active 状态**：使用 `Style::PrimaryBlue` 背景，显示打勾图标。
    - **Running 状态**：执行旋转动画显示 `refresh` 图标。
- **解耦 TODO**：在 `mousePressEvent` 中仅保留点击信号发射，具体的业务调用留空。

### 4.2 标题栏按钮注入 (对应用户原话：“图中红色圈着的这部分1”)
- **MainWindow 修改**：在 `setupCustomTitleBarButtons()` 中创建 `m_btnToggleDriveBar`。
    - **位置**：将其插入到标题栏按钮组的最左侧（紧贴 `m_btnSync`）。
    - **样式**：外框 `24x24px`（遵循 Memories.md 第 3 节），初始图标为 `chevrons_down`。
    - **交互**：连接 `toggled` 信号，用于控制 `m_driveBarWidget` 的显隐。

### 4.3 盘符管理栏布局复刻 (对应用户原话：“图中红色圈着的这部分2”)
- **MainWindow 结构扩展**：
    - 在 `setupSplitters()` 中，将 `m_driveBarWidget` 插入到 `m_navBarWidget` 与 `bodyWrapper` 之间。
    - **实现 `initDriveBar()`**：使用 `QHBoxLayout` 配合 `QDir::drives()` 自动填充 `DriveButton` 实例。
    - **初始状态**：检测物理路径（如 `D:\ArcMeta.Library_D`）是否存在，若存在则设为 `Active`，否则 `Inactive`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/DriveButton.h` / `src/ui/DriveButton.cpp`: 新建组件。
- [ ] `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`: 注入布局与切换逻辑。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `AutoImportManager` 的任务调度核心逻辑。
- [ ] 禁止修改 `MetadataManager` 的数据库存储格式。

## 6. 实现准则与预警【核心】

1.  **尺寸准则**：盘符按钮高度固定为 `28px`，容器高度固定为 `32px`，确保在 100% 缩放下的像素级对齐。
2.  **图标一致性**：由于当前版本 `SvgIcons.h` 已包含 `chevrons_down/up`，需确保移植后引用名称准确。
3.  **Memories.md 冲突检查**：标题栏按钮必须安装 `m_hoverFilter`（遵循 Memories.md 第 5 节），否则将丢失 ToolTip 提醒功能。
4.  **TODO 标注**：所有涉及业务调度的 slot 必须标注 `// TODO: Task 3 UI-Only version` 标识。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 标题栏按钮尺寸 | 固定为 `24x24px`，图标 `18x18px` | ✅ 符合 |
| 标题栏按钮样式 | 悬停 `#3E3E42`，按下 `#4E4E52` | ✅ 符合 |
| 输入框清除 | 仅限原生 `setClearButtonEnabled(true)` | ✅ 符合 |

## 8. 待确认事项
- 暂无。
