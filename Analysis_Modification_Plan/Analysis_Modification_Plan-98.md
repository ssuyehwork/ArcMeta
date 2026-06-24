# 托管文件夹、优先任务逻辑修正与内容面板拖拽修复 —— Analysis_Modification_Plan-98.md

## 1. 任务背景
修复近期新增的盘符管理栏功能（托管文件夹/优先任务）中的逻辑偏差与实现不完整问题，并根据“旧版本-7”的实现标准，物理修复内容面板（ContentPanel）中失效的文件/文件夹拖拽移动功能。

## 2. 问题定位
1.  **盘符右键菜单逻辑偏差**：`MainWindow::showDriveContextMenu` 目前未能精准根据 `ArcMeta.FERREX` 存在性切换菜单项，且插队逻辑未能在 UI 与后台调度间形成闭环。
2.  **USN 监听范围过大/偏移**：`AutoImportManager` 需精准收拢监听范围至托管文件夹内部，并正确触发入库（`ImportHelper`）与出库（`MetadataManager`）流程。
3.  **优先级复位机制缺失**：盘符按钮在所有任务完成后，未能自动恢复至默认的字母升序排列。
4.  **拖拽信号链路断裂**：审计确认 `ContentPanel` 在初始化 `DropTreeView` 和 `DropJustifiedView` 时，漏掉了 `pathsDropped` 信号的连接，导致放置操作被静默忽略。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 菜单显示“创建托管文件夹” / “打开托管文件夹” | `MainWindow::showDriveContextMenu` 逻辑重写 | ✅ |
| 2    | 每个盘符只允许存在一个 ArcMeta.FERREX | `createManagedFolder` 增加唯一性检查 | ✅ |
| 3    | 移入触发入库 / 移出执行出库逻辑 | `AutoImportManager` 对接 `ImportHelper` 与 `MetadataManager` | ✅ |
| 4    | 优先任务插队到盘符栏最左侧 | `MainWindow::reorderDriveButtons` 支持优先级置顶 | ✅ |
| 5    | 全部完成后优先级归零，恢复默认排列顺序 | 监听 `allTasksCompleted` 触发 UI 复位 | ✅ |
| 6    | 拖拽某个文件夹/文件到目标文件夹功能修复 | `ContentPanel` 补全信号连接与放置处理逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 托管文件夹菜单逻辑修正 (`MainWindow.cpp`)
1.  **动态菜单更新**（对应用户原话：“若该盘符根目录下不存在...菜单显示'创建托管文件夹'”）：
    - 在 `showDriveContextMenu` 中，使用 `QDir(letter + "/ArcMeta.FERREX").exists()` 精准判定。
    - 绑定“创建”动作：调用 `QDir().mkpath()` 并立即刷新 UI。创建前会检查文件夹是否已物理存在，以符合“禁止重复创建”的要求。
    - 绑定“打开”动作：调用 `QDesktopServices::openUrl()`。

### 4.2 优先任务 UI 插队、复位与动画反馈 (`MainWindow.cpp`)
1.  **插队逻辑**（对应用户原话：“直接插队到盘符栏最左侧”）：
    - 修改 `reorderDriveButtons`，使其根据 `m_priorityDrives` 列表重排按钮。
    - 重排算法：`[m_priorityDrives 中的项] + [按字母升序排列的其余盘符]`。
2.  **动画启动与停止**（对应用户原话：“当该盘符对应的入库任务正在执行时，盘符按钮上显示转圈动画”）：
    - 监听 `AutoImportManager::taskStarted(QString letter)` 信号：调用 `m_driveButtonMap[letter]->setLoading(true)` 开启 30ms 旋转动画。
    - 监听 `AutoImportManager::taskFinished(QString letter)` 信号：调用 `m_driveButtonMap[letter]->setLoading(false)` 停止动画。
3.  **自动复位**（对应用户原话：“所有任务完成后优先级归零，恢复默认排列顺序”）：
    - 监听 `AutoImportManager::allTasksCompleted` 信号：在槽函数中清空 `m_priorityDrives` 列表，并调用 `reorderDriveButtons()` 触发 UI 位置物理回位。

### 4.3 自动化监听与调度修正 (`AutoImportManager.cpp`)
1.  **路径过滤**（对应用户原话：“监听 ArcMeta.FERREX 文件夹内的变化”）：
    - 修正 `checkAndGetManagedPath`，确保只有路径前缀为 `X:\ArcMeta.FERREX` 且位于根目录的记录才被允许通过。
2.  **入库/出库联动**（对应用户原话：“自动触发扫描入库逻辑 / 联动执行出库逻辑”）：
    - `onEntryAdded`：在去抖后，将受影响路径喂给 `ImportHelper::importPaths()`。
    - `onEntryRemoved`：立即调用 `MetadataManager::instance().removeMetadataSync()` 或 `setInvalid()`（对应用户原话：“联动执行失效或移除出库逻辑”），不再使用误删除 API。
3.  **串行调度**：
    - 完善 `scheduleNextTask`，通过 `m_taskWatcher` 监听 `ImportHelper` 的 Future 状态，确保前一个盘符的任务彻底结束后再启动下一个。

### 4.4 修复内容面板拖拽功能 (`ContentPanel.cpp`)
1.  **信号补全**（对应用户原话：“拖拽某个文件夹/文件到目标文件夹功能也被破坏了”）：
    - 在 `initGridView` 和 `initListView` 中，显式连接：
      `connect(view, &DropTreeView::pathsDropped, this, &ContentPanel::onPathsDropped);`
2.  **放置逻辑实现**（参考“旧版本-7”）：
    - 新增 `onPathsDropped(const QStringList& paths, const QModelIndex& targetIndex)`。
    - 逻辑：
        1. 若 `targetIndex` 有效且指向文件夹：将 `paths` 移动至该文件夹下。
        2. 若 `targetIndex` 无效或指向空白：将 `paths` 移动至当前 `m_currentPath`。
        3. 调用 `BasicCommands::Move` (或现有等效 API) 执行物理移动并同步更新元数据路径。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.cpp / .h`：菜单、重排与信号接收。
- [ ] `src/core/AutoImportManager.cpp / .h`：过滤、调度与联动。
- [ ] `src/ui/ContentPanel.cpp / .h`：拖拽信号连接与业务槽函数。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MftReader.cpp` 的核心位图检索算法。
- [ ] 禁止在 `FERREX-Rust-原版/` 目录下进行任何修改。

## 6. 实现准则与预警【核心】
1.  **头文件依赖**：确保包含 `<QDesktopServices>`, `<QMimeData>`, `<QUrl>`, `<QFileInfo>`。
2.  **线程安全**：`AutoImportManager` 的任务分发必须在主线程触发 UI 动画，但扫描必须在 `QtConcurrent` 线程执行。
3.  **物理同步**：在执行拖拽移动后，必须调用 `MetadataManager::instance().moveMetadata()` 以防止元数据因路径改变而丢失。
4.  **考古对齐**：`onPathsDropped` 的处理逻辑必须对齐“旧版本-7”中对 `DropAction` 的判定，优先使用 `MoveAction`。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 动画实现 | 按钮使用 loading 动画表示任务中 | ✅ (DriveButton) |
| 拖拽逻辑 | 使用 pathsDropped 信号传递路径 | ✅ |
| 盘符展示 | Default 盘符带 ★ 前缀 (考古发现) | ✅ (将在修正中补全) |

## 8. 待确认事项
- **关于默认排列**：已确认恢复为盘符字母升序 (A-Z)。
- **动画时长**：`DriveButton` 的旋转定时器设为 30ms，保持平滑。
