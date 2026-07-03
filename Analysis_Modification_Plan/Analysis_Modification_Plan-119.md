# 实现内存与磁盘实时增量同步与秒退出架构 —— Analysis_Modification_Plan-119.md

## 1. 任务背景
当前程序采用“运行期仅写内存库 + 退出前全量同步”的架构，导致退出时产生严重的 I/O 延迟（弹出“正在保存数据”进度条），且运行期间数据落地不及时。为了在保证 UI 极致响应的同时确保数据安全，需重构为“内存优先、增量落盘”的架构。

## 2. 问题定位
- **核心组件：** `src/meta/DatabaseManager.cpp/h`、`src/meta/MetadataManager.cpp/h`、`src/ui/TrayController.cpp`。
- **瓶颈分析：** 
    - `DatabaseManager` 的全量备份机制导致了退出时的阻塞。
    - `MetadataManager` 原有的 `m_batchTimer` (1500ms) 导致了数据落地的物理延迟。
    - `TrayController` 的 `flushStep` 循环是导致“正在保存数据”进度条 UI 的直接源头。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 启动时数据库必须同步到内存中 | 启动阶段通过 `sqlite3_backup` 实现 Disk -> Memory 的全量加载 | ✅ |
| 2    | 运行期：内存同步到数据库 | 变更时优先提交内存事务，随后通过异步队列增量写入磁盘数据库 | ✅ |
| 3    | 内存优先：先更新内存库，再同步到物理磁盘 | 链路：`:memory: DB` -> `Disk DB` -> `m_cache` | ✅ |
| 4    | 退出程序时严禁耗时同步，必须实现秒退出 | 移除退出备份循环，数据已在运行期增量落地 | ✅ |
| 5    | 严禁脑补放弃使用 SQLite 内存模式 | 完整保留 `:memory:` 内存库连接及使用逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 启动期：磁盘全量加载至内存 (对应用户原话：“启动时数据库必须同步到内存中”)
- **职责实现**：在 `DatabaseManager::loadDb` 成功打开磁盘库 `conn.diskDb` 后：
    1.  显式打开 `:memory:` 库并赋值给 `conn.memDb`。
    2.  利用 `sqlite3_backup_init` 将 `conn.diskDb` (Source) 的内容全量复制到 `conn.memDb` (Destination)。
    3.  此步骤确保运行期所有高频读操作均面向高速内存库。

### 4.2 运行期：内存优先增量同步架构 (对应用户原话：“运行期：内存同步到数据库”、“内存优先：先更新内存库，再同步到物理磁盘”)
- **双句柄事务管理**：
    - `DatabaseManager` 维持对同一卷的 `memDb` (内存) 和 `diskDb` (磁盘) 两个活跃句柄。
- **实时同步链路设计**：
    - **Step 1 (Memory Commit)**：元数据修改函数（如 `setRating`）首先在 `conn.memDb` 句柄上执行 SQL 语句并提交。
    - **Step 2 (Async Dispatch)**：内存写入成功后，立即将该 SQL 指令或变更数据对象封装为任务，投递至后台异步 I/O 线程池。
    - **Step 3 (Disk Persistence)**：后台线程在 `conn.diskDb` 上执行同样的 SQL，实现物理落地。
    - **Step 4 (Final Update)**：在任务分发后（或磁盘反馈成功后），更新 C++ 层的 `m_cache` 镜像并通知 UI。
- **移除防抖延迟**：彻底移除 `MetadataManager` 中的 `m_batchTimer` (1500ms) 和 `m_dirtyPaths` 集合。所有修改必须“即改即分发”。

### 4.3 异步 I/O 队列与线程安全
- **任务队列**：在 `DatabaseManager` 或 `MetadataManager` 中引入 `std::deque<std::function<void()>> m_syncQueue`。
- **顺序写入保证**：使用专用的单线程消费者（或互斥锁保护的任务线程）确保对磁盘句柄的写入是顺序的，防止并发冲突导致 `SQLITE_BUSY`。
- **磁盘锁优化**：在磁盘连接上配置 `sqlite3_busy_timeout(conn.diskDb, 5000)`，增加 I/O 弹性。

### 4.4 TrayController 退出流程清理 (对应用户原话：“必须实现秒退出”、“彻底移除退出时的 flushStep 备份循环”)
- **移除阻塞 UI**：从 `onQuitApp` 中物理删除 `BatchProgressDialog` 相关的代码（对应截图中的“正在保存数据”对话框）。
- **即时关闭逻辑**：
    1.  调用 `MftReader::instance().clear()` 停止后台扫描。
    2.  等待异步同步队列中的残留任务完成（由于是增量，单次修改通常在毫秒级，队列极短，可瞬间完成）。
    3.  调用 `DatabaseManager::instance().shutdown()` 释放所有物理句柄，随后立即 `QApplication::quit()`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.cpp/h`：双连接管理、初始化全量备份 API。
- [ ] `src/meta/MetadataManager.cpp/h`：重构 `persistAsync` 逻辑，移除防抖计时器。
- [ ] `src/ui/TrayController.cpp`：精简退出流程。

**明确禁止越界修改的范围：**
- [ ] 禁止删除或忽略启动时的 `sqlite3_backup` 初始化。
- [ ] 禁止在 UI 主线程执行可能阻塞的磁盘 SQL 写入。

## 6. 实现准则与预警【核心】
- **依赖头文件**：必须包含 `<functional>`、`<deque>`、`<mutex>`、`<thread>` 及 `sqlite3.h`。
- **错误处理**：若磁盘增量同步失败（如磁盘满），必须在 `m_cache` 中记录“同步异常”标记，或通过日志及 UI 状态栏进行红色预警。
- **内存优先一致性**：确保内存库写入失败时，不应下发磁盘同步任务，以维持内存与磁盘的状态序列一致。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 程序秒退架构规约 | 彻底移除退出时的 flushStep 备份循环 | ✅ 符合 (步骤 4.4) |
| 实时同步架构 (内存优先) | 先更新内存库，再将变更同步到物理磁盘 | ✅ 符合 (步骤 4.2) |
| 同步按钮样式 | 存在待同步元数据时显示 ErrorRed | ✅ 符合 (步骤 6 预警逻辑) |

## 8. 待确认事项（可选）
- 异步队列在程序由于强行杀进程（Task Manager）导致的强制退出时，仍可能丢失极少量正在队列中的变更。
