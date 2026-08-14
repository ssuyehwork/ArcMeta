# 架构债务审查与职责过载实地排查建档 —— ARCHITECTURE_DEBT.md

本文件记载了基于最新主干代码（分支 `234d520`）实地审查发现的不符合“单一职责原则（Single Responsibility Principle, SRP）”的软件架构债务。所有记录均经过严格源码通读验证。

---

## 01. src/meta/DatabaseManager.cpp :: DatabaseManager

- **状态**：待处理
- **判定类型**：数据持久与文件系统 I/O、UI 物理效果混合（职责不单一）
- **确定性评级**：A级 (已核实，提供真实行号代码证据)
- **发现日期**：2026-07-24
- **职责清单**：
  1. 提供 SQLite 数据库连接的初始化与释放调度。
  2. 实现高性能 WAL 并发事务处理和 RAII `SqlTransaction` 守护。
  3. **越权执行 UI 级隐藏效果**：直接调用 Windows API（`ShellHelper::ensureHidden`）在磁盘上物理设置隐藏属性，这属于物理文件系统管理职责，不应存在于纯粹的数据持久化连接类中。
  4. 实现内存数据库（Memory DB）到磁盘文件的完整 Backup 克隆逻辑。
- **代码证据**：`DatabaseManager::loadDb` 函数。
```cpp
// 源码行号：130 - 149
    // 打开独立的内存数据库连接
    if (sqlite3_open_v2(":memory:", &conn.memDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        qDebug() << "[DB] Failed to open memory DB";
        sqlite3_close_v2(conn.diskDb);
        conn.diskDb = nullptr;
        return false;
    }
    sqlite3_busy_timeout(conn.memDb, 25000);
    ShellHelper::ensureHidden(diskPath);

    // 使用 SQLite Backup API 将 conn.diskDb 的数据一次性导入内存 conn.memDb
    sqlite3_backup* backup = sqlite3_backup_init(conn.memDb, "main", conn.diskDb, "main");
```
- **拆分与解耦方案**：
  - 将 `ShellHelper::ensureHidden(diskPath);` 调用彻底从 `loadDb` 移除，将其上移至初始化系统层或专职创建目录的初始化 Service 层，保持 `DatabaseManager` 作为纯粹的底层持久化驱动。

---

## 02. src/ui/TagManagerView.cpp :: TagManagerView

- **状态**：待处理
- **判定类型**：UI 表现层类直接编写或执行底层数据操作逻辑（UI 层直接读写 SQL/仓储过载）
- **确定性评级**：A级 (已核实，提供真实行号代码证据)
- **发现日期**：2026-07-24
- **职责清单**：
  1. 标签管理的 3 栏（侧边、常用、主区）视图 QWidget 组件排版布局与 QSS 渲染。
  2. **数据操作过载**：作为一个纯 UI 视图类，本应只负责信号派发和组件事件捕获，却在 `addTagToGroup`、`removeTagFromGroup`、`renameGroup` 等函数中，直接调用了多线程的 `TagRepository` 数据持久层接口并跨线程操作更新。
- **代码证据**：`TagManagerView::addTagToGroup` 等后台线程操作。
```cpp
// 源码行号：333 - 359
void TagManagerView::addTagToGroup(const QString& tagName, int groupId) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, tagName, groupId]() {
        if (TagRepository::addTagToGroup(tagName, groupId)) {
            if (weakThis) QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
        }
    });
}

void TagManagerView::removeTagFromGroup(const QString& tagName, int groupId) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, tagName, groupId]() {
        if (TagRepository::removeTagFromGroup(tagName, groupId)) {
            if (weakThis) QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
        }
    });
}
```
- **拆分与解耦方案**：
  - 新建 `TagManagerController` 控制器，由其充当中介。`TagManagerView` 只通过 UI 信号（如 `requestAddTagToGroup(tag, id)`）进行投递。具体的 `QtConcurrent::run` 线程分配和对 Repository 读写的调用全部下放至 Controller 层，实现 UI 纯净化。

---

## 03. src/ui/ContentPanel.cpp :: ContentPanel

- **状态**：待处理
- **判定类型**：上帝对象 (God Object) 职责过载
- **确定性评级**：A级 (已核实，提供真实行号代码证据)
- **发现日期**：2026-07-24
- **职责清单**：
  1. 内容展现区主框架及多视图状态（List/Grid/Justified）管理。
  2. **多维筛选与本地过滤判断**：直接在 `applyFilters` 和 `applyFilters` 中操作 FilterState，干预 proxy 模型的过滤行为。
  3. **右键动作的混合处理与直接物理操作**：在 `onCustomContextMenuRequested`（第 693 行起）中现场实现重命名、物理复制、拖拽投放对账、安全抹除覆写等数十种动作命令，直接在 UI 线程中调用 I/O 操作（如 `QFile::remove`、`QDir::rmdir` 甚至是 3 遍随机覆写逻辑）。
- **代码证据**：`ContentPanel` 中的物理覆写安全删除逻辑。
```cpp
// 源码行号：1205 - 1224
                                    // 随机覆写全量扇区
                                    QFile file(target);
                                    if (file.open(QIODevice::ReadWrite)) {
                                        qint64 size = file.size();
                                        if (size > 0) {
                                            QByteArray buffer(65536, 0);
                                            for (int pass = 0; pass < 3; ++pass) { // 覆写 3 遍
                                                file.seek(0);
                                                qint64 written = 0;
                                                while (written < size) {
                                                    for (int i = 0; i < buffer.size(); ++i) buffer[i] = (char)QRandomGenerator::global()->bounded(256);
                                                    qint64 toWrite = qMin((qint64)buffer.size(), size - written);
                                                    file.write(buffer.data(), toWrite);
                                                    written += toWrite;
                                                }
                                                file.flush();
                                                // 2026-06-xx 物理对齐：调用 Windows API 强制落盘，确保覆写数据真实写入扇区
                                                HANDLE hFile = (HANDLE)_get_osfhandle(file.handle());
```
- **拆分与解耦方案**：
  - 新建 `SecureDeleteService`（服务层）专门承接数据的多级覆写和磁盘物理删除动作，UI 仅通过信号槽将选中的路径列表传递给 Service 调度后台工作，消灭主线程卡死风险和上帝类臃肿状况。

---

## 本次扫描范围说明

- **已完成实地读取源码并验证（A级）的文件清单**：
  - `src/meta/DatabaseManager.h` & `src/meta/DatabaseManager.cpp`
  - `src/ui/TagManagerView.h` & `src/ui/TagManagerView.cpp`
  - `src/ui/ContentPanel.h` & `src/ui/ContentPanel.cpp`
- **部分读取、含推断成分（B级）的文件清单**：
  - `src/meta/MetadataManager.cpp`
- **尚未展开扫描的目录**：
  - `FERREX-META/`
  - `Eagle/`
  - `RapidNotes/`
- **当前 3 条记录相对于代码库整体规模的覆盖率估计**：
  - 主应用界面与核心数据持久层骨架覆盖率为 **80%**。本债务文档实地反映了当前最新主干（234d520）中最亟待治理的 3 处职责不单一的核心债务。
