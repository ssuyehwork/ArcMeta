# Analysis_Modification_Plan-102: 托管文件夹监听与任务控制功能优化

## 1. 任务背景与目标
在标题栏下方的盘符栏实现托管文件夹监听与任务控制功能。确保按钮状态机、右键菜单、USN 联动逻辑完全符合需求规格。

## 2. 状态机逻辑对账 (依据【一、盘符按钮状态机】)

| 状态 | 表现 | 转换触发 (左键单击) | 自动转换 |
| :--- | :--- | :--- | :--- |
| **状态A — 未激活** | 灰色 | 切换到**状态B** | - |
| **状态B — 已激活** | 蓝色 (静止) | 切换回**状态A**，停止监听 | 有入库任务时 -> **状态C** |
| **状态C — 任务执行中** | 蓝色 + 转圈动画 | 暂停当前任务 -> **状态D** | - |
| **状态D — 任务已暂停** | 暂停 SVG 图标 | 恢复任务执行 -> **状态C** | 任务队列完成 -> **状态B** |

- **持久化**: 激活状态写入 `AppConfig` 的 `"DriveBar/ActiveDrives"` (用户原话)。

## 3. 右键菜单逻辑 (依据【二、右键菜单】)
- **方位**: 对准盘符按钮 (用户原话: "对准盘符单击右键")。
- **内容分流**:
  - 若不存在 `ArcMeta.Library`: 显示 **"创建托管文件夹"** (用户原话)。
  - 若已存在 `ArcMeta.Library`: 显示 **"打开托管文件夹"** (用户原话)。
- **样式**: 复用 `UiHelper::applyMenuStyle()`。

## 4. USN 联动逻辑优化 (依据【三、USN Journal 监听与入库/出库联动】)
- **监听范围**: 仅限 `ArcMeta.Library` 文件夹内部变化 (用户原话: "仅监听已激活盘符根目录下的 ArcMeta.Library 文件夹内部变化")。
- **入库**: 必须复用现有 `ImportHelper::importPaths()` (用户原话: "复用现有 ImportHelper::importPaths() 流程")。
- **出库**: 复用现有 `MetadataManager::setInvalid()` 或 `removeMetadataSync()` (用户原话)。

## 5. 组件复用方案 (依据【四、盘符按钮 UI 规范】)
- **转圈动画**: 复用 `DriveButton` 已有的 `m_animationTimer` (每 30ms 旋转 10 度)。
- **暂停图标**: 复用 `SvgIcons.h` 中的 `"pause"` (用户原话: "使用现有 SvgIcons.h 中的暂停相关图标")。
- **按钮样式**: 严格复用现有 `DriveButton` 的 QSS 定义。

## 6. 修改步骤与范围 (依据【五、修改边界】)

### 第一步：修改 `src/core/AutoImportManager.cpp`
- **目标**: 纠正入库与出库的接口调用。
- **逻辑**:
  - `onEntryRemoved`: 移除对 `hasUserOperations` 的判断，直接执行失效处理。
  - `processImportQueue`: 将单条注册逻辑改为收集路径并调用 `ImportHelper::importPaths`。

### 第二步：优化 `src/ui/MainWindow.cpp`
- **目标**: 闭合状态机切换环路，确保状态 D (暂停) 逻辑生效。
- **逻辑**:
  - `onDriveButtonClicked`: 确保 A<->B 和 C<->D 的双向切换。
  - `onDriveButtonContextMenu`: 实现“创建/打开”分流。

### 第三步：验证持久化
- 检查 `loadActiveDrives` 是否正确恢复监听。

## 7. 待确认事项
- 目前 `DriveButton` 的 `setState` 逻辑已经包含了状态机的一部分，将通过 `MainWindow` 的点击事件进行统筹驱动。
