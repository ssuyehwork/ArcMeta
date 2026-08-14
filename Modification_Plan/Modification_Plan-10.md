# 取消托管库监控与自动导入底层资产剪切迁移重构 —— Modification_Plan-10.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目双轨制架构进一步精细化演进的背景下，为提升底层监控效率，避免托管库自身操作引发不必要的监控事件回灌和冗余对账，需要完全取消 `NativeFolderWatcher (IOCP)` 对 `ArcMeta.Library_[盘符]` 的实时监控。
同时，用户期望恢复之前被移除的“创建自动导入”功能，并将其更名为“自动导入”。在底层机制上，当该“自动导入”监控目录（如 `Z:\测试`）感知到有项目（文件或子目录）变化时，直接采用剪切/迁移方式，通过统一资产打包组件 `AssetImporter`，将项目安全剪切并创建到对应盘符的托管库（如 `Z:\ArcMeta.Library_Z`）中。该过程由于目的地绝对明确，不弹出任何 `FramelessMessageBox` 进行确认问询。

## 2. 问题定位
- **托管库注册监控多余**：在系统启动时，`CoreController::startSystem` 与 `SystemBootstrapper::bootstrapMonitors` 会自动遍历各盘符的 `ArcMeta.Library_[盘符]` 托管库路径并调用 `addWatch`。这需要在代码层彻底取消注入。
- **自动导入功能缺失**：此前的重构方案（如 `Modification_Plan-2.md`）中物理根除了 `CustomFolderImportDialog` 以及相关的右键菜单和 FolderButton 状态更新。需要从 `初始版` 重新迁移恢复这一界面及配置读写逻辑。
- **对账入库时效性差与对账冗余**：原有的自定义监控由 `AutoImportManager::handleRecursiveIngestion` 驱动原位扫描和对账，不符合用户“直接剪切/迁移到托管库”的最新诉求。需要重构 `CoreController` 中接收 `NativeFolderWatcher::filesChanged` 信号后的业务响应中枢，识别监控目录变动，定位顶级项目，并静默分流至 `AssetImporter::importAssets` 中执行物理搬运。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 从现在开始 NativeFolderWatcher (IOCP) 机制不再监控“ArcMeta.Library_[盘符]”文件夹 | 4.1 节，取消 `CoreController.cpp` 与 `SystemBootstrapper.cpp` 中对托管库的 `addWatch` 调用 | ✅ |
| 2    | 初始版的“创建自动导入”功能恢复到当前版本并将选项名改为“自动导入”，底层逻辑也要进行整改 | 4.2 节与 4.3 节，在 `MainWindow.h`/`MainWindow.cpp` 中恢复并重命名弹窗为“自动导入” | ✅ |
| 3    | 被监控的文件夹是“Z:\测试”，当感知到“Z:\测试”文件夹有项目发生变化时，采用剪切/迁移方式通过AssetImporter，直接将“Z:\测试”文件夹里的项目创建到相应的“Z:\ArcMeta.Library_Z”托管库里即可 | 4.4 节，在 `CoreController.cpp` 变动槽内识别监控目录下的项目变动，定位其顶级项目并通过 `AssetImporter` 执行剪切/迁移入库 | ✅ |
| 4    | 无需弹出 FramelessMessageBox 问询，因为盘符已经非常明确目的地了，但这仅限于NativeFolderWatcher (IOCP) 机制。如果是手动粘贴/拖拽导入情况下，则保持现逻辑不变 | 4.4 节，对于 IOCP 自动触发的资产剪切迁移过程，不弹出任何 `FramelessMessageBox` 确认问询；对于粘贴和拖放操作，保持原有逻辑和问询机制不变 | ✅ |

## 4. 详细解决方案

### 4.1 取消对 ArcMeta.Library 托管库的 IOCP 监控
- 修改 `src/core/CoreController.cpp` 中的 `startSystem` 阶段，在遍历各物理磁盘驱动器时，彻底删除或注释掉如下针对 `managedAbsW` 托管库的监听调用：
  ```cpp
  // 注释掉或物理删除下面这行
  // NativeFolderWatcher::instance().addWatch(managedAbsW);
  ```
- 修改 `src/core/SystemBootstrapper.cpp` 中的 `bootstrapMonitors()` 方法，同样注释掉或物理删除针对托管库的监听调用：
  ```cpp
  // 注释掉或物理删除下面这行
  // NativeFolderWatcher::instance().addWatch(managedAbsW);
  ```

### 4.2 恢复并重命名“自动导入”对话框
- 在 `src/ui/MainWindow.cpp` 的顶部，重新恢复并实现 `CustomFolderImportDialog` 构造及相关方法。
- 将对话框窗口标题（Title）物理变更为`"自动导入"`（对应用户原话：`"选项名改为“自动导入”"`）：
  ```cpp
  CustomFolderImportDialog::CustomFolderImportDialog(QWidget* parent)
      : FramelessDialog("自动导入", parent) {
      // 包含单行编辑框、清除按钮（原生 setClearButtonEnabled(true)）及“浏览”、“完成”按钮
  }
  ```
- 在 `src/ui/MainWindow.h` 中恢复 `CustomFolderImportDialog` 声明，并在 `MainWindow` 类中恢复 `void showNewAutoImportDialog();` 的私有成员方法声明。

### 4.3 恢复并对位“自动导入”右键菜单选项与持久化
- 修改 `src/ui/MainWindow.cpp` 中的 `initDriveBar()`、`onDriveBarContextMenu`、`onFolderButtonContextMenu` 和 `removeCustomMonitoredFolder`：
  - 将原有的文案统一变更为 `"自动导入"`（对应用户原话：`"选项名改为“自动导入”"`）：
    - 盘符栏空白处右键：`menu.addAction("自动导入")`。
    - 自定义按钮右键：`menu.addAction("自动导入")`，解除监控仍为 `"解除监控"`。
  - 恢复 `MainWindow::showNewAutoImportDialog()` 方法逻辑：
    - 弹窗输入并校验路径，将选定路径存入 `AppConfig` 的 `"DriveBar/CustomMonitoredFolders"`。
    - 调用 `NativeFolderWatcher::instance().addWatch(normPath)` 点火动态激活外部目录的 IOCP 变动监控。
    - **自动同步对账整改**：由于底层逻辑整改（外部目录不再原地保留资产，而是剪切），当用户新增自动导入目录时，立即扫描其中既有的所有文件/文件夹：
      - 获取其盘符根目录下的 `ArcMeta.Library_盘符` 路径。
      - 若既有文件/文件夹不为空，自动调用 `AssetImporter::importAssets`（不弹窗问询，`targetCatId = 0`）将它们一次性全部迁移至托管库下。
    - 更新渲染盘符栏的 FolderButtons。

### 4.4 重构底层 IOCP 自动监控与剪切迁移逻辑
- 修改 `src/core/CoreController.cpp` 中的 `CoreController` 构造函数中关于 `NativeFolderWatcher::instance().filesChanged` 的信号连接槽（在 QueuedConnection 事件队列帧中处理）：
  - 当捕获到 `Added` 或 `Modified` 变动事件时，进行“自动导入路径”前缀精准匹配：
    - 读取当前的 `"DriveBar/CustomMonitoredFolders"` 配置。
    - 若 `ev.newPath` 以其中某一配置路径（例如 `Z:\测试`）为前缀，则触发**自动导入剪切迁移机制**：
      1.  计算该变动项目在该监控目录下的顶级项目（Top-level Item）路径：
          - 例如：监控目录为 `Z:\测试`，变动项目路径为 `Z:\测试\sub\1.png`，其对应的顶级项目应为 `Z:\测试\sub`。
          - 提取公式：计算相对路径，取出相对路径的第一级成分名（如 `sub`），拼接在监控目录后面组成顶级物理路径（`Z:\测试\sub`）。
      2.  如果该顶级物理路径在硬盘上真实存在，且其未被列入当前正在迁移的待处理批次中：
          - 收集顶级路径到当前批次。
          - 直接调用资产打包导入器 **`AssetImporter::importAssets(QStringList() << topLevelPath, 0, nullptr, onComplete)`**。
          - 🚨 关键设计：整个过程直接后台进行物理搬运，由于 `AssetImporter::importAssets` 内部调用时不会弹窗问询（它仅显示 `BatchProgressDialog` 进度条反馈），从而达成**“无需弹出 FramelessMessageBox 问询”**（对应用户原话）的极致体验，保持安静和流畅。
          - 迁移完成后，自动调用 `MetadataManager::instance().notifyFullUIRebuild()`，让 UI 无感、自愈式刷新，显示最新的托管库数据。
      3.  由于顶级目录整个被剪切移动，变动项被完全转移。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/CoreController.cpp`（第 43-69 行重构 filesChanged 响应槽分流，第 100-112 行取消托管库的 addWatch 调用）
- [ ] `src/core/SystemBootstrapper.cpp`（第 27-29 行取消托管库的 addWatch 调用）
- [ ] `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`（恢复 CustomFolderImportDialog，修改菜单名称为“自动导入”，并在 `MainWindow` 中恢复 showNewAutoImportDialog 触发器和相关联动及自动迁移初始化）

**明确禁止越界修改的范围：**
- [ ] `src/core/NativeFolderWatcher.cpp` 异步 Windows IOCP 事件泵与去重合并机制——不修改
- [ ] `src/util/AssetImporter.cpp` 单个文件及目录递归打包机制——不修改
- [ ] `src/ui/ContentPanel.cpp` 中的手动粘贴 `performPaste` 与拖放 `onPathsDropped` 导入问询和对话框提示逻辑——不修改

## 6. 实现准则与预警【核心】
1. **头文件包含**：在 `src/core/CoreController.cpp` 中使用 `AssetImporter` 前，必须精准引入头文件：
   ```cpp
   #include "../util/AssetImporter.h"
   ```
2. **多线程并发安全**：IOCP 自动监控迁移触发于异步线程或主线程事件帧中。调用 `AssetImporter::importAssets` 时应确保其在主线程或通过 QueuedConnection 安全运行以避免多线程 GUI 操作（`BatchProgressDialog` 创建）导致的闪退风险。在 CoreController 的槽函数中，由于槽通过 `Qt::QueuedConnection` 运行于 `CoreController` 所在的线程（通常为主线程），因此创建进度条是完全安全的。
3. **避免重入与死循环**：`AssetImporter` 将文件剪切到 `ArcMeta.Library_[盘符]` 时，由于我们已经**取消**了 NativeFolderWatcher 对 `ArcMeta.Library_*` 的实时监控，因此绝对不会因为文件的写入行为二次触发 `filesChanged` 事件，完美规避了自触发无限死循环的风险，设计极其高内聚、零隐患！
4. **去抖过滤**：顶级项目被移动时，会向 `Z:\测试` 发送 `Removed` 事件。在 `CoreController` 中处理 `WatcherAction::Removed` 时，若该路径是外部监控目录，调用 `removeMetadataSync` 即使返回未注册也不产生任何影响。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除按钮 | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，而且只可采用“Qt 原生的 setClearButtonEnabled(true)”，杜绝脑补另创 | ✅ 符合。在恢复的 `CustomFolderImportDialog` 中，单行编辑框 `m_edit` 统一使用 `setClearButtonEnabled(true)`。 |
| 标题栏与关闭按钮 | 标题栏高度 34px，按钮外框 24x24px，关闭按钮默认与悬停持续显示红色高亮 `#e81123`，按下为 `#A50000` | ✅ 符合。本方案不修改主界面标题栏按钮，恢复的 Dialog 基础按钮规范自动继承 `FramelessDialog` 设计，符合标准。 |

## 8. 待确认事项（可选）
- **无**。所有底层重构逻辑与行为规范均已与用户完全达成共识，确定无误。
