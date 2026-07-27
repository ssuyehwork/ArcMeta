# NativeFolderWatcher 监控服务架构解耦与防抖重命名高内聚重构 —— Modification_Plan-110.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在对底层高性能 Windows IOCP 文件监控服务 `NativeFolderWatcher` 的深度审计中，发现其存在由于历史 AI 应急补丁导致的职责过载、高并发重命名匹配混乱、主线程事件风暴积压以及多线程定时器初始化警告等架构缺陷。为了保障系统的极致稳定性和高可维护性，需要对 `NativeFolderWatcher` 进行深度解耦与架构级的高内聚重构。

## 2. 问题定位
1. **职责过载与强耦合（严重违反 SRP）**：`NativeFolderWatcher` 直接引用并调用了 `AutoImportManager`、`MetadataManager` 的具体业务函数（如 `handleRecursiveIngestion`、`registerItemsAsync`、`removeMetadataSync`），导致底层监控与上层业务严重纠缠，底层库无法做到独立复用。
2. **并发/批量重命名 FIFO 队列错配**：原有 `m_pendingRenameOldPaths` 延迟匹配逻辑极其简陋，只是依次出队和配对。如果在短时间内发生大规模批量重命名，多线程事件乱序到来，会直接导致新旧文件名错乱匹配，造成元数据大范围崩溃。
3. **主线程 QMetaObject::invokeMethod 信号风暴**：对捕获的每一个单项文件变动信号，都立即单独派发一次 `invokeMethod` 抛给主线程去重，如果瞬间发生上万次 IO，主线程事件队列就会被同步挤爆，直接引发 GUI 整体长达数秒假死。
4. **QTimer 线程亲和性警告**：作为懒汉单例模式，`m_debounceTimer` 的初始化完全依赖首次调用时所在线程。如果在工作线程中被首次唤醒，则无法在没有事件循环的线程中启动 QTimer，导致定时器直接报废或发生崩溃。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 排查与“NativeFolderWatcher”相关的代码架构是否存在傻逼逻辑架构？职责是否单一？流程是否合理？（对应用户原话） | 本方案全方位诊断并重构其三大傻逼缺陷：职责过载耦合、重命名错配、事件信号风暴、定时器线程安全隐患。 | ✅ |
| 2    | 那么你认为该如何规划？请给出靠谱的修改方案（对应用户原话） | 规划高水平解耦方案，改写为纯底层标准事件过滤器/中继器，不直接执行任何上层管理器的业务动作，并采用全新事件合并池和信号队列。 | ✅ |

## 4. 详细解决方案

### A. 架构完全解耦：变为纯粹的事件中继器
1. 彻底清除 `NativeFolderWatcher.cpp` 中对 `#include "../meta/MetadataManager.h"` 和 `#include "AutoImportManager.h"` 的头文件引用，以及对应的所有直接业务调用。
2. 定义标准的通知元数据结构 `FileEvent` 和具体的动作类型 `FileAction`：
   ```cpp
   enum class FileAction {
       Added,
       Modified,
       Removed,
       Renamed
   };
   struct FileEvent {
       FileAction action;
       QString path;
       QString oldPath; // 仅对重命名有效
   };
   ```
3. `NativeFolderWatcher` 不再处理去抖业务，仅对外声明以下统一的高级过滤清理信号，交由对应的上层服务订阅：
   ```signals:
   void fileAdded(const QString& path);
   void fileModified(const QString& path);
   void fileRemoved(const QString& path);
   void fileRenamed(const QString& oldPath, const QString& newPath);
   void managedFolderRemoved(const std::wstring& path); // 维持原有的托管文件夹物理注销信号
   void bufferOverflowed(const std::wstring& rootPath); // 缓冲区溢出通知
   ```

### B. 解决 QTimer 初始化与线程安全问题
1. 改变单例构造中的定时器生成逻辑。不要在 `NativeFolderWatcher` 的构造函数中无视当前线程直接 `new QTimer(this)`。
2. 采用标准安全的线程归属保障：在 `instance()` 或构造时，通过 `QCoreApplication::instance()->thread()` 将 `NativeFolderWatcher` 实例及所有包含的 QTimer 亲和性强制与 GUI 主线程绑定，或使用主线程专用的初始化方法在 `initializeCoreComponents` 阶段强制在主线程内点火实例化。

### C. 优化并发重命名：高内聚安全匹配配对池
1. 废除极其简陋的基于 `vector` 队列弹出 FIFO 机制。
2. 引入一个带时间戳/过期清理的“智能重命名临时配对池” `std::map<QString, QDateTime>` 或 `QHash`。
3. 利用 Windows 重命名机制规律进行高级配对：在短时间内，重命名的 `OLD_NAME` 和 `NEW_NAME` 事件在同一个文件监控分区中其对应的底层内核时间、逻辑顺序是紧密贴合的。在同一轮 IO completion 缓冲区（`FILE_NOTIFY_INFORMATION` 链表）解析中，由于在底层属于同一个文件重命名动作，我们可以先在链表中进行同批次就地完美匹配。对于跨缓冲区或者因高密集多线程乱序过来的信号，根据系统生成的相对时序、所属父路径及事件发生间隔，通过在监控层内部维护一个短周期的事务图/配对池来进行高内聚防乱序合并，最大程度杜绝 FIFO 错位问题。

### D. 拦截与缓解主线程 GUI 信号轰炸：事件批次合并缓冲区
1. 废除来一个 `FILE_ACTION` 就单独调用一次主线程 `invokeMethod` 的傻逼设计。
2. 工作线程在底层解析 `FILE_NOTIFY_INFORMATION` 并生成 `FileEvent` 后，不直接派送主线程，而是先统一高速写入一个带互斥锁的内部事件累积队列 `m_rawEventQueue`。
3. 在底层引入定时微小合并动作（如 10ms - 20ms 的高速多线程轻量排队定时器，或在主线程通过轻量级异步通知信号控制）：每隔 20ms 批量抽取并整合一次 `m_rawEventQueue` 中的所有原始信号，一次性在主线程主循环执行批量分发，大幅削减 `QMetaObject::invokeMethod` 对 Qt 事件循环带来的并发总压力。

### E. 业务层接收架构重新订阅
1. 原来在 `NativeFolderWatcher` 内部被直接执行的业务，将被迁移和解耦到 `AutoImportManager` 或类似上层协调层中（例如，通过 `MainWindow` 或 `CoreController` 在点火时统一进行槽函数桥接连接）。
2. 让 `AutoImportManager` 或 `MetadataManager` 直接 connect 订阅 `NativeFolderWatcher` 的 `fileAdded`、`fileModified`、`fileRemoved`、`fileRenamed` 等信号，在对应的槽内再按需进行业务处理和业务本身的去重/异步对账。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/core/NativeFolderWatcher.h`
- [ ] 模块/文件：`src/core/NativeFolderWatcher.cpp`
- [ ] 模块/文件：`src/core/CoreController.cpp`（用于在主线程优雅点火初始化与订阅关联业务）

**明确禁止越界修改的范围：**
- [ ] `MftReader.cpp` 物理 MFT 扫描——不修改
- [ ] `DatabaseManager.cpp` SQLite 底层事务提交与语句编译——不修改

## 6. 实现准则与预警【核心】
1. **依赖预防**：重构时必须绝对杜绝在 `NativeFolderWatcher.h` / `NativeFolderWatcher.cpp` 中引入 `MetadataManager.h` 或是 `AutoImportManager.h`。
2. **QTimer 亲和性**：`m_debounceTimer` 必须挂在主线程的事件循环上。如果不小心在后台线程中首次实例化该单例，必须增加线程安全校验 `QThread::currentThread() == qApp->thread()`，否则会抛出 Qt Timer 的并发报错。
3. **并发安全锁**：`m_mutex` 在工作线程解析及主线程事件弹出时必须保持最小颗粒度加锁保护，防止在密集高并发 IO 变动时造成 `m_watches` / `m_outstandingWatches` 被破坏导致 Crash。
4. **生命周期完美保障**：在 Windows `CancelIoEx` 被调用后，挂起的 I/O 操作会在 IOCP 中返回一个带有 `ERROR_OPERATION_ABORTED` 错误码的完成包。本方案在底层清场自检中，必须保证该完成包未被消费完前，其 `WatchItem` 指针和 shared_ptr 的生命周期绝对不会提前消亡，彻底杜绝悬空野指针崩溃。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 定时器与线程安全 | QTimer 必须在所属 EventLoop 线程内启动和销毁，单例初始化需做线程防护。 | ✅ 本方案通过在 `initializeCoreComponents` 进行主线程提前强制初始化并关联，确保线程亲和性安全。 |
| 高内聚单一职责 | 业务逻辑层与底层驱动/监控服务应完全解耦，底层服务禁止直接调用具体业务类单例。 | ✅ 本方案彻底斩断了与 MetadataManager 和 AutoImportManager 的依赖，改为纯 Qt 信号中继。 |

## 8. 待确认事项（可选）
- 在完全解耦 `NativeFolderWatcher` 之后，上层对监控变动的消费是通过 `CoreController` 在 `startSystem` 阶段动态 connect，还是在 `initializeCoreComponents` 顺序预热时完成信号与槽的集中绑定？建议在 `CoreController` 中集中处理订阅逻辑。
