# 托盘退出导致后台进程残留与文件占用分析 (Analysis_Modification_Plan-14)

## 1. 现象描述
用户从托盘菜单执行“退出”后，托盘图标消失，但进程（ArcMeta.exe）依然驻留在后台，且数据库文件（Arcmeta_*.db）显示被占用无法删除。

## 2. 根因剖析 (Root Cause Analysis)

### 2.1 同步持久化阻塞 (The SQLite Backup Bottleneck)
- **触发路径**: `TrayController::onQuitApp` -> `QApplication::quit()` -> `DatabaseManager::~DatabaseManager` -> `flushAll()`。
- **物理成因**: 
  - `flushAll()` 是一个纯同步函数，它调用 `sqlite3_backup` 将数据从内存库写回磁盘。
  - 对于 **5 万条规模** 的扫描数据，SQLite 的页面拷贝和磁盘 I/O 需要显著的时间（约数秒至数十秒，取决于硬件）。
  - 在此期间，主线程被挂起，进程句柄无法释放，导致外部观察到“文件被占用”。

### 2.2 异步清理链的冲突
- **逻辑**: `MftReader::clear()` 内部使用了 `QtConcurrent::run` 来异步停止线程。
- **冲突**: 虽然 `clear()` 返回得很快，但 `QApplication` 在退出时会等待所有 `QtConcurrent` 线程结束（这是 Qt 的默认行为）。如果后台线程正在进行繁重的 USN 扫描或数据存盘，退出动作会被无限期推迟。

### 2.3 UI 线程与进程生命周期的脱节
- **现状**: 托盘图标 `m_trayIcon->hide()` 发生在 `QApplication::quit()` 之前。
- **后果**: 给用户造成了“程序已关闭”的错觉，而实际上底层持久化逻辑才刚刚开始进入高负载写入阶段。

## 3. 解决方案 (Proposed Fixes)

### 3.1 退出流程重构
- **解法**: 废除析构函数中的同步 `flushAll`。
- **实施**: 在 `onQuitApp` 中首先弹出“正在安全保存数据并退出...”的模态提示，阻止用户二次操作，并在 UI 线程显式、步进式地执行 `flushAll`。

### 3.2 强力中断机制
- **UsnWatcher 优化**: 为后台扫描线程增加 `AtomicBool m_abort`。在每处理一批数据后强制检查该位。
- **SQLite 优化**: 将 `sqlite3_backup_step` 的参数从 `-1` (一次性全部) 修改为分段拷贝（如 `50` 页一跳），并在跳跃间隙调用 `processEvents`，保持响应性。

### 3.3 物理对标
- **文件占用解决**: 确保在 `QApplication::quit()` 真正被调用前，所有 `sqlite3*` 指针已完成 `sqlite3_close_v2` 调用。

## 4. 结论
目前的现象是**“重负载持久化”**期间触发了**“隐式同步等待”**。这证明系统正在忠实地执行数据保护逻辑，只是在用户体验上缺乏必要的进度反馈和强制中断能力。
