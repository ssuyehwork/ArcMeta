# Analysis_Modification_Plan-129: USN 实时监控引擎“点火”补完方案

## 1. 任务背景
在执行 Plan-126 架构迁移（全系统转向单一 USN Journal 主轨）后，用户反馈“拖入文件夹完全没有反应”，但“重启后有效”。经排查确认为：系统虽建立了信号连接，但未在启动链条及交互关键点调用 `MftReader::buildIndex` 或 `loadFromCache`。这导致 `UsnWatcher` 线程从未启动，实时监听功能处于物理瘫痪状态。

## 2. 问题定位
- **信号链“断头路”**：`AutoImportManager::startListening()` 仅建立了 Qt 信号槽连接，但驱动该信号的底层 `UsnWatcher` 线程需要由 `MftReader` 显式启动。当前 `src/` 代码库中缺失了所有启动入口调用。
- **静态扫描掩盖故障**：用户看到的“重启有效”实际上是 `CoreController::startSystem()` 中执行的 `syncAllManagedLibraries()` 静态递归扫描所致。该扫描是快照式的，无法替代运行时的实时监控。
- **职责真空区**：在 `MainWindow` 创建托管库的交互逻辑中，仅进行了数据库登记和物理创建，未同步激活该盘符的 USN 监控引擎。

## 3. 强制对照表
| 用户原始描述 | 架构事实诊断 | 严重程度 |
| :--- | :--- | :--- |
| “将其他文件夹拖入之后没有任何反应” | `UsnWatcher` 线程未 `start()`，没有任何 USN 信号产生 | 阻塞级 |
| “重启后才有反应” | 属于“情况 A”：来自启动时的全量静态对账，而非实时监控 | 逻辑误导 |
| “全卷监控 + 精准路径过滤” (规约) | 入口调用点缺失，代码实现与规约存在“真空层” | 架构违规 |

## 4. 详细解决方案

### 4.1 启动链条“点火” (CoreController 补完)
在系统异步初始化链条中，补齐 MFT 缓存加载逻辑。
- **修改位置**：`src/core/CoreController.cpp` 的 `startSystem()` 函数。
- **逻辑描述**：在 `AutoImportManager::instance().startListening()` 之后，立即调用 `MftReader::instance().loadFromCache()`。
- **预期效果**：程序启动时，自动为所有历史已建立索引的盘符挂载 `UsnWatcher` 线程。

### 4.2 交互激活“热插拔” (MainWindow 补完)
在创建托管库的瞬时，激活实时监控引擎。
- **修改位置**：`src/ui/MainWindow.cpp` 的 `onDriveButtonContextMenu` 函数。
- **逻辑描述**：在“创建托管文件夹”分支（`val == 1`）成功创建目录并登记分类后，立即调用 `MftReader::instance().buildIndex({letter})`。
- **预期效果**：用户新建库后，无需重启程序，实时监控立即生效。

### 4.3 信号闭环校验 (AutoImportManager 审计)
确保 `isUnderManagedLibrary` 的过滤逻辑不会因索引未就绪而误判。
- **逻辑描述**：由于 `buildIndex` 可能需要时间，确保 `AutoImportManager` 的槽函数在 `MftReader` 索引构建完成前不会因为 `getIndexByKey < 0` 而静默失效（目前代码已有此检查，需保持）。

## 5. 修改边界
- **受控文件**：
  - `src/core/CoreController.cpp` (补全启动入口)
  - `src/ui/MainWindow.cpp` (补全交互激活)
- **非受控区**：
  - `MftReader.cpp` 与 `UsnWatcher.cpp` 逻辑已验证正确，严禁改动。
  - `MetadataManager.cpp` 的注册逻辑已验证正确，保持不动。

## 6. 实现准则与预警
- **【强制】单次点火原则**：严禁在 `onDriveButtonClicked`（点击盘符）中重复调用 `buildIndex`，必须通过 `isDriveIndexed()` 进行预检，防止产生重复的监控线程。
- **【预警】异步死锁**：`MftReader::loadFromCache` 内部会获取 `QWriteLocker`。由于 `CoreController` 在后台线程执行初始化，必须确保此时没有 UI 线程在同步阻塞等待 `MftReader` 的数据，防止死锁。
- **【考古对标】**：`MainWindow` 的 `buildIndex` 调用应参照 `旧版本-6` 的实现方式。

## 7. Memories.md 合规检查
- [x] **USN Journal 监控激活机制**：符合“MftReader::buildIndex 或 loadFromCache 是启动监控的唯一指令入口”的规约。
- [x] **单一数据入库主轨红线**：本方案旨在修复该主轨的“动力源”问题，确保物理位移能正确触发 USN 信号。
- [x] **“盘符栏”逻辑架构规约**：点击/右键操作需同步更新盘符状态并触发引擎启动。
