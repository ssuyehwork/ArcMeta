# 托管文件夹自动管理与任务执行体系 —— Analysis_Modification_Plan-108.md

## 1. 任务背景
在主界面标题栏下方重建“盘符管理栏”，通过 `pending_imports` 缓冲区实现物理变动（USN Journal 监听）与执行逻辑（入库/出库）的深度解耦。该体系需具备四态状态机切换、断点续传、以及基于 Win32 API 的实时路径反查能力。

## 2. 问题定位
- **UI 缺失**：当前版本 `MainWindow` 缺少 `m_driveBarWidget` 容器及折叠按钮。
- **数据层缺失**：需在 SQLite 中引入 `pending_imports` 任务表。
- **逻辑层重构**：`AutoImportManager` 需从简单的 USN 监听升级为具备“生产者（监听标记）- 消费者（任务执行）”模型的管理中心。
- **底层强化**：`MftReader` 需新增基于 FRN (FileReferenceNumber) 的实时路径反查接口。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在 DatabaseManager::init() 中负责建表 | 修改 `DatabaseManager::init` 插入建表语句 | ✅ |
| 2    | 状态C — 蓝色转圈（任务执行中） | 实现 `DriveButton` 旋转动画绘制 | ✅ |
| 3    | 状态D — 灰色暂停图标 (pause) | 使用 `SvgIcons::pause` 绘制灰色背景图标 | ✅ |
| 4    | 必须通过 NtQueryInformationFile 或 FSCTL_READ_FILE_USN_DATA 实时查询 | 在 `MftReader` 实现基于 `FILE_ID_DESCRIPTOR` 的路径查表 | ✅ |
| 5    | 托管文件夹路径构建逻辑只允许存在于一处 | 统一定义 `QString managedPath = drive + "ArcMeta.Library_" + driveLetter;` | ✅ |
| 6    | 监听到移入：status=1；监听到移出：status=-1 | `AutoImportManager` 的 USN 回调逻辑 | ✅ |
| 7    | 批量调用现有 ImportHelper::importPaths() | 状态 C 下的入库执行流 | ✅ |
| 8    | 调用现有 MetadataManager::setInvalid() | 状态 C 下的出库执行流 | ✅ |

## 4. 详细解决方案

### 4.1 数据层：缓冲区表结构
- **操作**：修改 `src/meta/DatabaseManager.cpp`。
- **实现**：在 `init()` 函数中执行 `CREATE TABLE IF NOT EXISTS pending_imports (...)`。
- **语义**：
    - `status=1`: 监听到变化，待处理。
    - `status=2`: 处理完成（入库成功）。
    - `status=-1`: 待出库（文件删除）。

### 4.2 UI 层：盘符栏与 DriveButton 状态机
1. **新建 `DriveButton` 类** (继承 `QPushButton`)：
    - **成员变量**：`m_state`, `m_rotationAngle`, `m_animationTimer`。
    - **绘制逻辑**：
        - `paintEvent`：根据 `m_state` 填充背景色（Inactive: #333, others: PrimaryBlue/Gray）。
        - **旋转实现** (对应用户原话：“QTimer 驱动旋转角度变化”)：`Running` 状态下定时器每 30ms 触发 `m_rotationAngle += 10` 并调用 `update()`。
    - **状态定义**：严格对齐 A(灰静)、B(蓝静)、C(蓝转)、D(灰暂)。

2. **MainWindow 布局重建**：
    - **容器**：在 `setupSplitters` 中 `m_titleBarWidget` 与 `m_navBarWidget` 之间插入 `m_driveBarWidget` (42px)。
    - **折叠按钮**：标题栏右侧添加 `m_btnToggleDriveBar`，图标使用 `chevrons_down`。
    - **点击流转** (对应用户原话)：实现 A->B/C, B->A, C->D, D->C 的逻辑判定。

### 4.3 监听层：USN Journal 路径实时查表
- **MftReader 扩容**：
    - 新增 `QString getPathByFrn(HANDLE hVol, DWORDLONG frn)`。
    - **技术红线** (对应用户原话：“必须通过 NtQueryInformationFile 或 FSCTL_READ_FILE_USN_DATA”)：使用 `OpenProcessToken` 获取特权，通过 `FSCTL_READ_FILE_USN_DATA` 获取 `USN_RECORD_V2` 中的 ParentFRN，配合 `NtQueryInformationFile` 的 `FileNameInformation` 级别递归上溯，禁止访问内存索引。
- **AutoImportManager 标记逻辑**：
    - 仅在 USN 回调中执行 `REPLACE INTO pending_imports`，不触发入库。

### 4.4 执行层：任务消费逻辑
- **生产者消费模型**：
    - 当盘符进入状态 C：
        - 开启一个异步线程/Task。
        - **入库**：`SELECT path FROM pending_imports WHERE drive=? AND status=1` -> `ImportHelper::importPaths` -> 更新 `status=2`。
        - **出库**：`SELECT path FROM pending_imports WHERE drive=? AND status=-1` -> `MetadataManager::setInvalid` -> `DELETE` 条目。
- **右键菜单** (对应用户原话)：
    - 实现“创建托管文件夹”：`QDir().mkdir(managedPath)`。
    - 实现“重新扫描”：执行 `UPDATE pending_imports SET status=1 WHERE drive=? AND status=2`。

## 5. 修改边界声明【红线】

**允许修改：**
- [x] `src/ui/MainWindow.cpp` (添加布局与状态控制)
- [x] `src/ui/DriveButton.h/cpp` (新增控件)
- [x] `src/core/AutoImportManager.cpp` (核心标记与消费逻辑)
- [x] `src/meta/DatabaseManager.cpp` (建表)
- [x] `src/mft/MftReader.cpp` (仅限新增路径反查接口)

**明确禁止修改：**
- [ ] 禁止修改 `ImportHelper.cpp`。
- [ ] 禁止在 `MainWindow` 以外的地方构建托管文件夹路径名。
- [ ] 禁止在 `ContentPanel` 等其他面板注入任何逻辑。

## 6. 实现准则与预警【核心】
1. **参数化**：`AutoImportManager` 必须通过 `startTask(const QString& managedPath)` 接收参数。
2. **复用**：打开文件夹必须调用 `ShellHelper::openInExplorer`。
3. **动画**：`DriveButton` 的转圈必须平滑，退出 `Running` 状态时必须停止定时器。
4. **Win32 特权**：路径反查涉及系统底层，需确保 `SE_BACKUP_NAME` 和 `SE_RESTORE_NAME` 权限已开启。

## 7. Memories.md 合规检查
- **盘符激活异步化**：必须使用 `QtConcurrent::run` 启动执行层任务，防止 UI 假死。
- **USN 掩码**：确保 `MftReader` 的 USN 监听掩码包含 `USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME` 等。

## 8. 待确认事项
- 确认 `SvgIcons.h` 中的 `pause` 图标是否符合视觉预期。（已确认存在 `pause` 键值）。
- 确认旋转动画是否需要反向旋转（方案默认为顺时针）。
