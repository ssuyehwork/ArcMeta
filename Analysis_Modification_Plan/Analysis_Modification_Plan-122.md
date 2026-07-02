应用逻辑架构与僵尸代码清理分析 —— Analysis_Modification_Plan-122.md

1. 任务背景
针对当前 FERREX 项目中存在的架构冗余、功能断层以及无效代码残留问题进行专项审计。重点解决“神类”MainWindow 的职责过载、导入引擎不统一导致的“傻逼逻辑”，以及界面残留但底层失效的“僵尸代码”问题。

2. 问题定位
- **架构缺陷 (MainWindow 神类)**：`src/ui/MainWindow.cpp` 直接处理了驱动器盘符菜单逻辑、文件系统路径拼接、甚至是元数据数据库的直接读写（如 1599-1608 行），严重违反了 UI 与业务逻辑分离的原则。
- **逻辑断层 (TODO 菜单)**：盘符栏右键菜单的“重新扫描该盘”仅有 UI 入口和调试输出，核心业务逻辑处于缺失状态。
- **僵尸代码 (进度环)**：`src/core/IndexedEntry.h` 中的 `registrationProgress` 字段及 `ThumbnailDelegate.cpp` 中的渲染逻辑虽然存在，但全局没有任何地方对该进度值进行实际计算和赋值。
- **模块冗余 (导入引擎)**：`src/util/ImportHelper.cpp` 负责手动移动与注册，而 `src/core/AutoImportManager.cpp` 负责自动同步，两者逻辑高度重复且未归口统一。

3. 强制对照表
编号	用户原话 / 我的理解	方案对应点	是否一致
1	审核整个应用的逻辑架构存在多少傻逼逻辑架构？	第 4 节：定位 MainWindow 职责过载及导入引擎分裂问题	✅
2	存在多少僵尸代码？	第 4.2 节：明确指出 registrationProgress 进度环为僵尸代码	✅
3	哪些逻辑架构需要合并成模块？	第 4.3 节：建议将 ImportHelper 与 AutoImportManager 合并为摄取引擎模块	✅
4	“重新扫描该盘” 应该是扫描该盘符下的“ArcMeta.Library_[盘符]”文件夹里所有的数据	第 4.1 节：明确全量扫描的业务作用域并记录至规划文档	✅

4. 详细解决方案
4.1 业务定义补完（对应用户原话：““重新扫描该盘” 应该是扫描该盘符下的“ArcMeta.Library_[盘符]”文件夹里所有的数据”）
- 在 `Development_Plan.md` 中已明确该功能的作用域为全量物理文件夹扫描。
- 实施路径：在 `MainWindow::onDriveButtonClicked` (val == 3) 分支中，不再直接编写逻辑，而是调用 `CoreController` 的 `triggerRescan(driveRoot)` 接口。

4.2 僵尸代码清理（针对用户原话：“存在多少僵尸代码？”）
- **识别项**：`src/core/IndexedEntry.h` 中的 `double registrationProgress`。
- **处理建议**：在未实现第 4.3 节的统一计算引擎前，该字段及其在 `ThumbnailDelegate` 中的绘制逻辑应标记为“Deprecated”或清理，防止 UI 误导。

4.3 模块归口合并（针对用户原话：“哪些逻辑架构需要合并成模块？”）
- **合并目标**：将 `ImportHelper` 的物理操作逻辑与 `AutoImportManager` 的数据库同步逻辑封装为统一的 `IngestionEngine`（摄取引擎）。
- **职责划分**：
    - `IngestionEngine`：负责 0/1 标记位的数据库原子操作、全量/增量扫描、元数据解析。
    - `NativeFolderWatcher`：仅负责监听并向 `IngestionEngine` 发送信号。
    - `MainWindow`：仅通过 `CoreController` 触发命令，严禁直接读写数据库。

5. 修改边界声明【红线】
本次方案涉及范围：
- 规划记录：`Development_Plan.md`
- 架构建议：`src/ui/MainWindow.cpp`, `src/util/ImportHelper.cpp`, `src/core/AutoImportManager.cpp`
明确禁止越界修改的范围：
- 禁止直接删除正在使用的数据库字段，除非已完成合并后的逻辑替换。
- 禁止修改任何现有的 Win32 原生调用逻辑（如 MFT/USN 监听部分）。

6. 实现准则与预警【核心】
- **预警**：目前 `MainWindow` 的代码量已接近 2000 行，任何解耦操作必须配合 `CoreController` 的接口扩展同步进行，否则会导致 UI 逻辑彻底瘫痪。
- **UI 考古**：关于进度环的绘制，必须参考 `ThumbnailDelegate.cpp` 中现有的 `drawRegistrationProgress` 函数，确保合并后的逻辑能正确驱动该 UI 组件，而不是重新实现绘制代码。

7. Memories.md 合规检查
组件 / 模式	Memories.md 规范要求	本方案是否符合
UI 与逻辑分离	UI 严禁直接访问 Repo 或 Manager	✅（提倡收拢至 CoreController）
摄取标记位 (0/1)	必须通过原子函数同步物理与数据库状态	✅
统一引擎	全应用禁止出现两套入库逻辑	✅

8. 待确认事项
- 在合并 `ImportHelper` 与 `AutoImportManager` 时，是否需要保留 `ImportHelper` 原有的“跨盘符移动”特定处理逻辑？（建议保留作为摄取引擎的底层工具函数）。
