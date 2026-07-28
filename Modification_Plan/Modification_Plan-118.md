# 侧边栏 `fullRecount()` 启动全量扫描增量判断机制加固 —— Modification_Plan-118.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的架构中，程序启动后或元数据加载完毕后，`CategoryPanel` 的刷新定时器会无条件触发 `CategoryRepo::fullRecount()` 进行全量重算。然而，`fullRecount()` 在内存数据加载完成后，会通过后台异步线程获取当前全量内存快照，并针对每一个文件通过 Windows API (`fetchWinApiMetadataDirect`) 轮询磁盘盘点校验文件的物理有效性。
由于没有增量判断逻辑，即便自上次退出到这次启动之间，用户磁盘监控目录完全没有发生任何变化，程序依旧会硬跑一遍全量扫描、物理盘点，白白浪费启动耗时并产生极高的 I/O 消耗。因此，必须引入轻量级状态指纹（Last Monitored mtimes）比对层，作为 `fullRecount()` 执行前的阻断门槛。

## 2. 问题定位
- 关键函数：`CategoryRepo::fullRecount()`（位于 `src/meta/CategoryRepo.cpp`）。
- 拦截缺失点：`fullRecount()` 唯一的准入限制是元数据必须已加载完成：
  ```cpp
  if (!MetadataManager::instance().isLoaded()) { ... return; }
  ```
  加载完成后，即刻无条件通过 `getLightweightCacheSnapshot()` 拿到快照并分流启动 `QtConcurrent::run([db, snapshot]() { ... WinAPI 物理检验探测 ... })` 异步全量盘点，没有评估监控目录在此期间是否发生过实质改变。
- 根因分析：缺乏全局监控目录的时间戳 (mtime) 记录与验证。实际上，本软件只监控几种特定物理目录：托管库（如 `ArcMeta.Library_X`）与用户指定的自定义监控目录（`DriveBar/CustomMonitoredFolders`）。如果我们能在每次成功完成对账重算、或者在实时运行的监控变动完成时，把各监控根目录的当前物理 `mtime` 保存到 `AppConfig`；并在下次启动/刷新要重算前快速读取并比对，若没有任何目录的物理修改时间变化，即可直接退出重算流程，从而完美阻断全量扫描探针的运行。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 因为这套架构里根本没有"增量判断"这一层,`fullRecount()` 被设计成无条件全量扫描,没有任何机制去判断"上次退出到这次启动之间,磁盘到底有没有变化"。 (对应用户原话) | 在 `fullRecount()` 执行之初加入增量判断，若监控根目录没有变化则直接提前退出。 | ✅ |
| 2    | 正常这类"目录同步"架构，要避免每次启动全量扫描，通常靠状态指纹比对，比如给每个监控根目录存一个"上次扫描时间戳/版本号"。(对应用户原话) | 将所有被监控的根目录在成功盘点后记录其磁盘物理最后修改时间戳（mtime）作为轻量指纹持久化至 AppConfig，下次重算前读取比对。 | ✅ |
| 3    | 启动 → 无条件触发全量扫描/全量核对，不管有没有变化都要跑一遍，白白消耗启动时间和磁盘 I/O。(对应用户原话) | 在拦截成功时，不仅不启动本线程的重算、落库，更不会启动后台异步 WinAPI 盘点检测线程，彻底消灭启动期的无谓 I/O。 | ✅ |

## 4. 详细解决方案

### 4.1 核心状态指纹提取与判定机制
我们要设计的拦截机制核心为：在比对时判断 **“当前所有监控根目录的物理 `mtime` 指纹集合”** 是否与 **“上一次成功记录并持久化的指纹集合”** 完全相同。

1. **确定监控目录范围**：
   与系统启动 IOCP 监控一致，监控的根目录来源于：
   - 各个驱动器下的托管库文件夹：在 Windows 上，例如 `D:\ArcMeta.Library_D`（其绝对路径可通过 `MetadataManager::getManagedLibraryPath` 获取）。
   - 用户配置的自定义监控目录：在 `AppConfig` 的 `"DriveBar/CustomMonitoredFolders"` 中。

2. **状态指纹定义**：
   指纹是一个由监控根目录绝对路径和其物理 `mtime`（修改时间，用 `QFileInfo::lastModified().toMSecsSinceEpoch()` 表达）拼装成的 JSON 字符串或 Key-Value 键值对，持久化保存于 `AppConfig`（如键名为 `"Recount/LastMonitoredFingerprints"`）。

3. **判定拦截时机**：
   在 `CategoryRepo::fullRecount()` 函数的最开头，执行“增量指纹核对”。
   - 快速遍历当前所有活动的监控根目录，获取它们的物理绝对路径和当前最新的 `mtime`。
   - 读取 `AppConfig` 中保存的指纹。
   - 如果两者数量和值完全吻合（即路径完全一致，修改时间戳完全吻合），表示磁盘自上次同步至本次启动之间**未发生任何目录变动**。此时 `qDebug()` 输出提示并**直接 `return` 拦截**，不再执行任何内存去重、落库和后台异步 WinAPI 物理对账探测。
   - 如果不吻合，说明磁盘发生了变化。我们需要继续向下执行原有的全量对账和探针校验，并在原有的 `trans.commit();` 提交事务持久化成功后，**立即更新最新的指纹数据并落盘持久化**，以供下次作为新比对基准。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/CategoryRepo.cpp` 中的 `CategoryRepo::fullRecount()`
  - 修改此函数。在开头读取并比对监控根目录的修改时间指纹，若一致则输出日志并直接提前退出。若不一致，在完成盘点并成功 commit 事务后，将当前最新的指纹集合更新持久化回 `AppConfig`。

**明确禁止越界修改的范围：**
- [ ] `MetadataManager::getLightweightCacheSnapshot()` —— 不修改
- [ ] `fetchWinApiMetadataDirect` 探测函数本身 —— 不修改
- [ ] `CategoryPanel` 刷新定时器调度机制 —— 不修改

## 6. 实现准则与预警【核心】

1. **依赖的头文件预警**：
   为了读取和比对监控根目录的修改时间指纹，我们需要使用 `QFileInfo` 和 `QDateTime` 提取时间戳，以及 `AppConfig` 用于持久化和读取，因此必须在 `CategoryRepo.cpp` 中引入相关头文件：
   ```cpp
   #include "../core/AppConfig.h"
   #include <QFileInfo>
   #include <QDateTime>
   #include <QJsonDocument>
   #include <QJsonObject>
   ```
2. **监控目录的获取对齐**：
   在 `CategoryRepo::fullRecount()` 中，应按照与 `CoreController::startSystem()` 完全相同的逻辑来收集当前的监控根目录，防止出现遗漏：
   - 遍历系统所有驱动器 `QDir::drives()`，获取其托管库绝对路径。
   - 遍历 `AppConfig` 的 `"DriveBar/CustomMonitoredFolders"`。
3. **性能零损耗**：
   收集目录 `mtime` 是针对根目录的极轻量级操作（仅获取根节点 meta），不会引起整棵目录树的 I/O 递归扫描，性能消耗几乎为零。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 指纹比对机制 | 状态指纹必须持久化至可靠配置服务中，且核对逻辑应与核心监控驱动源保持高度一致。 | ✅ 是。指纹将作为 `"Recount/LastMonitoredFingerprints"` 存储于全局 `AppConfig` 配置服务中，且监控目录的获取逻辑与系统 IOCP 监控完全保持同频对齐。 |

## 8. 待确认事项（可选）
（无）
