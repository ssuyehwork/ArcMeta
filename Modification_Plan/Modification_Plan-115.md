# NativeFolderWatcher 底层监控高性能物理重构与解耦 —— Modification_Plan-115.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
本方案承接自 `Modification_Plan-114.md` 的分析结论，并根据用户的明确授权指令（对应用户原话："无需理会，直接修改" 以及 "我让你直接按照修改方案去实施，知道吗？"），将 NativeFolderWatcher 纯净解耦以及相关逻辑缺陷修复转化为物理代码实施。我们将对 `NativeFolderWatcher` 实施彻底的高内聚解耦与高性能批次事件发射重构。

## 2. 问题定位

通过对 `src/core/NativeFolderWatcher.h` 与 `src/core/NativeFolderWatcher.cpp` 展开逐行深度审计，我们排查到了以下核心架构缺陷与不合理逻辑设计：

### 缺陷一：严重违反单一职责原则（SRP）且与业务层高强度循环双向耦合
- **核心根因**：`NativeFolderWatcher` 作为最底层的“操作系统级 IO 文件系统监控者”，理应只负责监听 IO 信号，却直接硬编码调用了业务层的逻辑单例：`MetadataManager` 和 `AutoImportManager`。

### 缺陷二：多线程 CAS 锁与跨线程信号投递机制的过度依赖与潜在瓶颈
- **核心根因**：每一个解析到的文件变更都通过 `QMetaObject::invokeMethod` 抛回到 GUI 线程处理，极易在主线程中产生海量的事件队列风暴，导致 GUI 瞬间失去响应、假死。

### 缺陷三：缓冲区溢出检测触发“全量级联扫描”导致系统假死与性能雪崩
- **核心根因**：当监控缓冲区溢出时，旧代码直接发起高密度的全量级联扫描和数据库全量比对，让繁忙的系统瞬间假死。

### 缺陷四：重命名匹配机制对多队列与滑窗延迟的不可靠依赖
- **核心根因**：依赖 `50ms` 的单队列 FIFO 进行重命名配对极其脆弱，极易因为事件乱序或延迟而产生张冠李戴的数据错配与误判物理删除。

### 缺陷五：防抖去重引擎 QSet 限制导致的内存膨胀和高密集去重性能退化
- **核心根因**：去重队列在 200ms 后在主线程中同步遍历，由于没有真正合并压缩与分批推送，依然导致了通知风暴。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 无需理会，直接修改（对应用户原话："无需理会，直接修改"） | 突破之前分析方案中“不作物理改动”的限制，对相关核心代码文件执行物理修改与解耦重构。 | ✅ |
| 2    | 直接按照修改方案去实施（对应用户原话："我让你直接按照修改方案去实施，知道吗？"） | 严格根据重构图纸，将纯净事件订阅发布、主线程缓冲定时推送、超时精确双向关联池、溢自适应兜底等方案物理落地。 | ✅ |

---

## 4. 详细解决方案

### 第一步：完全剥离业务层头文件依赖，定义纯净事件总线
1. 彻底从 `NativeFolderWatcher.cpp` 中移除 `#include "../meta/MetadataManager.h"` 和 `#include "AutoImportManager.h"` 的头文件依赖。
2. 在 `NativeFolderWatcher.h` 中定义纯净的 Qt 自定义事件结构体与动作枚举：
   ```cpp
   enum class WatcherAction {
       Added,
       Modified,
       Removed,
       Renamed
   };

   struct FileWatcherEvent {
       WatcherAction action;
       QString oldPath;
       QString newPath;
       bool isDirectory;
   };
   ```
3. 在 `NativeFolderWatcher` 中提供唯一的批次发射信号：
   ```cpp
   signals:
       void filesChanged(const QList<ArcMeta::FileWatcherEvent>& events);
   ```

### 第二步：加装工作线程并发事件缓冲区与 100ms 主线程高内聚合并管道
1. 在 `NativeFolderWatcher` 内部维护一个受互斥锁保护的并发事件包缓冲区：
   ```cpp
   std::mutex m_eventMutex;
   QList<FileWatcherEvent> m_pendingEvents;
   ```
2. IOCP 工作线程解析完 `FILE_NOTIFY_INFORMATION` 缓冲区后，不再调用 `invokeMethod` 发射单条消息，而是直接将其封装为 `FileWatcherEvent` 并压入并发事件包缓冲区，极大地释放了工作线程开销并消除了跨线程通知。
3. 设立 `m_batchTimer` 定时器（100ms 周期），在 timeout 信号绑定的主线程槽函数 `processBatchQueue()` 中拉取缓冲区所有事件，并在主线程中开展高内聚、高安全性的防抖去重、重命名超时配对。

### 第三步：重构重命名超时事务精确关联池 (带 50ms 精确滑动超时判定)
1. 在主线程时钟周期内，通过一个精确追踪事务映射池进行匹配：
   ```cpp
   struct PendingRename {
       QElapsedTimer timer;
       bool isFolder;
       // 支持双向配对
   };
   std::map<QString, PendingRename> m_renamePool;
   ```
2. 当收到 `FILE_ACTION_RENAMED_OLD_NAME`（或被判定为疑似删除的信号）时，不立刻触发清退，而是记录到 `m_renamePool` 并启动 `timer`。
3. 当在 50ms 内收到对应的 `FILE_ACTION_RENAMED_NEW_NAME` 时，在精确映射池中优先进行后缀/名对齐配对，若匹配成功则无损生成一个 `WatcherAction::Renamed` 事件。
4. 在每个定时器周期结束时，遍历 `m_renamePool`，若有项超时 50ms 未匹配，则将其安全结算为 `WatcherAction::Removed` 事件分发。
5. 这一高内聚处理完全消除了 FIFO 乱序错配和多定时器线程安全隐患。

### 第四步：缓冲区溢出检测自适应弹性标记
1. 当发生 `bytesTransferred == 0` 时，不再原地强行级联全量扫描，而是将该 `WatchItem` 所属的根路径标记为 `ReloadRequested`（通过发送特殊的 `WatcherAction::Modified` 动作，且包含 isDirectory = true，或者发射专属通知），交由业务层判定空闲时再行低优先级扫描，避免对繁忙物理磁盘落井下石。

### 第五步：在 CoreController.cpp 中实现非侵入式桥接连接
1. 在 `CoreController::startSystem()` 初始化完成后，在主线程建立对 `NativeFolderWatcher::instance()` 的 `filesChanged` 信号的连接：
   ```cpp
   connect(&NativeFolderWatcher::instance(), &NativeFolderWatcher::filesChanged, this, [](const QList<ArcMeta::FileWatcherEvent>& events) {
       for (const auto& ev : events) {
           if (ev.action == ArcMeta::WatcherAction::Added || ev.action == ArcMeta::WatcherAction::Modified) {
               if (ev.isDirectory) {
                   std::wstring fullPath = ev.newPath.toStdWString();
                   (void)QtConcurrent::run([fullPath]() {
                       AutoImportManager::instance().handleRecursiveIngestion(fullPath);
                   });
               } else {
                   MetadataManager::instance().registerItemsAsync(QStringList() << ev.newPath, true);
               }
           } else if (ev.action == ArcMeta::WatcherAction::Removed) {
               std::wstring fullPath = ev.newPath.toStdWString();
               emit NativeFolderWatcher::instance().managedFolderRemoved(fullPath);
               MetadataManager::instance().removeMetadataSync(fullPath);
           } else if (ev.action == ArcMeta::WatcherAction::Renamed) {
               std::wstring oldW = ev.oldPath.toStdWString();
               std::wstring newW = ev.newPath.toStdWString();
               MetadataManager::instance().syncAfterMove(oldW, newW);
           }
       }
   }, Qt::QueuedConnection);
   ```
2. 这既完成了底层组件的彻底物理去耦，又保持了原有业务逻辑的无损契合与平滑集成。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/core/NativeFolderWatcher.h` （重构，完全切断业务依赖，新增缓冲字段、定时器、批次信号）
- [x] 模块/文件：`src/core/NativeFolderWatcher.cpp` （重构，实现 100ms 定时主线程管道、去重合并、事务配对池）
- [x] 模块/文件：`src/core/CoreController.cpp` （修改，桥接底层批次信号到业务层单例，确保业务集成）

**明确禁止越界修改的范围：**
- [ ] 物理 MFT 读取模块 `src/core/MftReader.cpp` —— 不修改
- [ ] SQLite 底层驱动及持久化核心逻辑 `src/meta/MetadataManager.cpp` 内部的核心 SQL 事务 —— 不修改
- [ ] UI 渲染及视图交互面板 `src/ui/ContentPanel.cpp` —— 不修改

---

## 6. 实现准则与预警【核心】

1. **精确依赖原则**：在 `NativeFolderWatcher.h` 中引入 `<QList>`、`<QElapsedTimer>` 支持高频配对与批次分发。
2. **多线程安全性**：在 IOCP `workerThread()` 中，将事件推入并发事件缓冲区时，必须使用高效独占锁 `m_eventMutex`。
3. **QObject 线程亲和性警示**：`m_batchTimer` 的 timeout 信号必须安全绑在主线程。严禁跨非 QThread 的 C++ `std::thread` 线程操作 Qt 计时器。
4. **开箱即用与高度自检**：重组过程优先保持兼容性，保证桥接订阅机制平滑过渡，防止发生任何编译错误。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| **侧边栏分类模式** | 所有具备“作用域”的功能（包括统计、监控反馈），其执行范围必须与 UI 顶部的 Focus Line 实时对齐，对托管文件夹及库进行精确过滤和响应。 | ✅ 符合。本方案重构为底座级纯净 IO 信号发布者，对托管物理文件夹（Library）的增删信号通过通用的 `managedFolderRemoved` 与 `filesChanged` 分发，完全剥离了业务侧判定，保持了对侧边栏和磁盘模式的对齐兼容性。 |
| **异步 IO 监控与防抖** | 采用高内聚以路径为 Key、以定时器关联的去重延迟合并，大幅削减短时间内向主线程投递高密集 `invokeMethod` 信号的风暴，杜绝 FIFO 错配。 | ✅ 符合。通过 100ms 批次定时器内合并去重防抖，彻底消除了连续 I/O 带来的 UI 通知风暴。 |

---

## 8. 待确认事项（可选）
- **无**。
