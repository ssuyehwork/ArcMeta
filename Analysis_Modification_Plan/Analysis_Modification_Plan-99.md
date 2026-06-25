# 托管文件夹监听与入库行为归一化 —— Analysis_Modification_Plan-99.md

## 1. 任务背景
用户希望新增“托管文件夹”功能，在盘符栏实现对 `ArcMeta.Library` 文件夹的自动监听。该功能需与现有的“扫描入库”及“拖拽导入”行为逻辑归一化，通过 `AutoImportManager` 驱动任务流，并实现一套完整的盘符按钮状态机（激活、监听、执行、暂停）。

## 2. 问题定位
- **UI 层**：`MainWindow::setupDriveBar` 目前使用标准 `QPushButton`，需替换为 `DriveButton` 以支持状态切换和动画；`onDriveButtonClicked` 槽函数目前为 TODO，需实现状态流转逻辑。
- **逻辑层**：`AutoImportManager` 现有逻辑较为简单（仅监听 `entryAdded`），需重构以支持：
    - 针对特定盘符的监听开关（状态 B 与 A 的切换）。
    - 任务队列的暂停与恢复（状态 C 与 D 的切换）。
    - `ArcMeta.Library` 路径精准过滤（对应用户原话：“仅监听已激活盘符根目录下的 ArcMeta.Library 文件夹内部变化”）。
    - 出库联动（复用 `MetadataManager::setInvalid`）。
- **持久化**：激活状态需写入 `AppConfig` 的 `DriveBar/ActiveDrives` 键。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 状态B — 已激活：USN Journal 开始监听该盘符的 ArcMeta.Library 文件夹 | `MftReader::updateActiveDrives` 与 `AutoImportManager` 路径过滤 | ✅ |
| 2    | 状态C — 任务执行中：自动切换到状态C（蓝色 + 转圈动画） | `DriveButton::setLoading(true)` | ✅ |
| 3    | 状态D — 任务已暂停：持续显示暂停 SVG 图标 | `DriveButton` 新增 `setPaused` 状态并绘制 `pause` 图标 | ✅ |
| 4    | 激活状态必须持久化到 AppConfig：key 格式：DriveBar/ActiveDrives | `MainWindow::loadActiveDrives` 实现 | ✅ |
| 5    | 右键菜单：创建托管文件夹 / 打开托管文件夹 | `MainWindow::onDriveButtonContextMenu` 实现，复用 `UiHelper::applyMenuStyle()` | ✅ |
| 6    | 监听范围：仅监听已激活盘符根目录下的 ArcMeta.Library 文件夹内部变化 | `AutoImportManager::onEntryUpdated` 路径前缀判定 | ✅ |
| 7    | 触发出库：复用现有 MetadataManager::setInvalid() 或 removeMetadataSync() | `AutoImportManager::onEntryRemoved` 逻辑实现 | ✅ |
| 8    | 转圈动画：复用现有实现 | 复用 `DriveButton` 现有 `m_animationTimer` | ✅ |

## 4. 详细解决方案

### 4.1 UI 层：盘符按钮状态机增强
1. **DriveButton 扩展**：
   - 在 `DriveButton` 类中新增 `enum State { Inactive, Active, Running, Paused }`。
   - 实现 `setState(State state)` 函数，根据状态切换背景色（Inactive: #333333, others: PrimaryBlue）及图标内容。
   - `paintEvent` 增强：
     - `Running` 状态下绘制旋转的 `refresh` 图标（对应用户原话：“转圈动画”）。
     - `Paused` 状态下绘制静止的 `pause` 图标（对应用户原话：“持续显示暂停 SVG 图标”）。

2. **MainWindow 逻辑接入**：
   - **初始化**：在 `setupDriveBar` 中将 `QPushButton` 替换为 `DriveButton`（参考 `src/ui/DriveButton.cpp`）。
   - **持久化恢复**：从 `AppConfig` 读取 `DriveBar/ActiveDrives`（对应用户原话：“应用重启后恢复上次的激活状态”），循环调用 `onDriveButtonClicked`。
   - **点击流转**：
     - `Inactive` -> `Active`：通知 `AutoImportManager` 开启该盘符监听。
     - `Active` -> `Inactive`：通知 `AutoImportManager` 关闭监听。
     - `Running` <-> `Paused`：通知 `AutoImportManager` 切换任务队列挂起状态。

3. **右键菜单实现**：
   - 监听 `customContextMenuRequested` 信号。
   - 使用 `QDir::exists(drive + "/ArcMeta.Library")` 判定状态。
   - 创建 `QAction`，分别绑定 `QDir().mkdir()` 或 `QDesktopServices::openUrl()`。
   - 调用 `UiHelper::applyMenuStyle(menu)`（对应用户原话：“样式必须复用现有 UiHelper::applyMenuStyle()”）。

### 4.2 逻辑层：AutoImportManager 任务驱动重构
1. **监听加固**：
   - 修改 `AutoImportManager` 构造函数，订阅 `MftReader` 的 `entryAdded`、`entryRemoved` 和 `entryUpdated`。
   - 在 `onEntryAdded/Updated` 中判定路径是否以 `[Drive]:/ArcMeta.Library/` 开头（对应用户原话：“仅监听已激活盘符根目录下的 ArcMeta.Library 文件夹内部变化”）。

2. **出库联动**：
   - 在 `onEntryRemoved` 中，通过 `MetadataManager::instance().getMeta(path)` 检查是否受控。
   - 若受控且位于 `ArcMeta.Library` 中，调用 `MetadataManager::instance().setInvalid(path, true)`（对应用户原话：“复用现有 MetadataManager::setInvalid()”）。

3. **任务控制**：
   - 维护 `std::unordered_map<QString, bool> m_drivePausedMap`。
   - `processImportQueue` 执行前检查对应盘符的暂停状态。若暂停，保留队列项不处理（对应用户原话：“新移入的文件继续排队，等待恢复后处理”）。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.h/cpp` (盘符栏逻辑)
- [ ] 模块/文件：`src/ui/DriveButton.h/cpp` (动画与状态绘制)
- [ ] 模块/文件：`src/core/AutoImportManager.h/cpp` (监听与任务流转)

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `src/mft/MftReader.cpp` 中的 USN 原始解析算法。
- [ ] 禁止修改 `src/util/ImportHelper.cpp` 的核心导入事务。
- [ ] 禁止修改任何分类面板（`CategoryPanel`）的 UI 布局。

## 6. 实现准则与预警【核心】
1. **头文件依赖**：`MainWindow.cpp` 必须包含 `#include "DriveButton.h"` 和 `#include "../core/AutoImportManager.h"`。
2. **图标获取**：确保 `SvgIcons::icons.contains("pause")`，若使用 `pause_filled` 请核对 QSS 颜色注入。
3. **线程安全**：`AutoImportManager` 的 `m_pendingPaths` 访问必须由 `std::lock_guard<std::mutex>` 保护，防止 MFT 信号与去抖计时器冲突。
4. **Win32 预警**：创建文件夹操作需考虑 UAC 权限，但在本程序已声明 `requireAdministrator` 的前提下可直接调用 `QDir().mkdir()`。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 盘符管理栏点击逻辑 | 负责数据库挂载、MFT 掩码更新及启动监听 | ✅ 符合 |
| 托管文件夹管理规范 | 根目录下唯一的 `ArcMeta.Library` | ✅ 符合（用户原话修正了名称为 ArcMeta.Library） |
| 动画驱动 | 旋转动画复用现有实现 | ✅ 符合 |
| QSS 样式 | 悬停背景 #3E3E42，按下 #4E4E52 | ✅ 符合 |

## 8. 待确认事项
- 用户提到的暂停图标在 `SvgIcons.h` 中有 `pause` 和 `pause_filled` 两个版本，方案默认采用 `pause` 以保持描边风格一致，请确认是否需要切换。
- 在状态 D（暂停）切换到状态 B（完成）的逻辑中，用户原话提到“任务队列全部完成 → 切换回状态B”，这意味着 `AutoImportManager` 需要在队列清空时通过信号通知 UI。
