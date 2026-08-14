# 职责不单一 —— Violates SRP

本文件记载了基于当前版本主干源码深度排查发现的不符合“单一职责原则（Single Responsibility Principle, SRP）”的软件架构债务。所有记录均经过严格源码分析核实，没有进行任何代码文件的物理修改。

---

## 01. src/ui/CategoryPanel.cpp :: CategoryPanel

- **状态**：待处理
- **判定类型**：2.1 (God Object) 及 2.3 (数据层与业务层混杂)
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **职责清单（穷举当前承担的所有职责）**：
  1. 侧边栏分类面板 UI 布局管理、折叠动画、行内编辑及右键菜单构建。
  2. **直接调用 sqlite3 C-API 执行数据写入**：直接越权管理数据库句柄并编译 SQL 事务、手动执行 reorder 操作，破坏了表现层与持久层的绝对物理隔离。
- **代码证据**：`CategoryPanel::reorderAll`。
```cpp
// 源码行号：1075 - 1089
void CategoryPanel::reorderAll(int parentId) {
    auto db = DatabaseManager::instance().connection().memDb;
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE categories SET sort_order = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        qWarning() << "[CategoryPanel] Failed to prepare reorder SQL:" << sqlite3_errmsg(db);
        return;
    }
```
- **拆分建议**：
  - 新建 `CategoryUiController` / 重构 `CategoryRepo` 将全部 `sqlite3_*` API 与底层排序事务彻底沉淀至数据仓储层，UI 仅通过参数向外转发，消灭原生 SQL 混合。

---

## 02. src/ui/ContentPanel.cpp :: ContentPanel

- **状态**：待处理
- **判定类型**：2.1 (God Object) 职责过载 及 2.3 (数据层与业务层混杂)
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **职责清单（穷举当前承担的所有职责）**：
  1. 内容展现区主视窗及多视图（List/Grid/Justified）的动态切换排版。
  2. **直接调用物理文件 I/O 阻塞操作**：在响应右键菜单时，在主线程同步执行递归删除文件夹以及三遍随机覆写安全粉碎逻辑。
  3. 直接调度 CategoryRepo 数据库写连接对资产进行分类重划。
- **代码证据**：`ContentPanel` 中的彻底递归删除。
```cpp
// 源码行号：2234 - 2248
                            // 普通彻底递归删除
                            std::function<bool(const QString&)> recursiveRemove;
                            recursiveRemove = [&](const QString& target) -> bool {
                                QFileInfo info(target);
                                if (info.isDir()) {
                                    QDir dir(target);
                                    for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                                        recursiveRemove(target + "/" + entry);
                                    }
                                    return QDir().rmdir(target);
                                } else {
                                    return QFile::remove(target);
                                }
                            };
```
- **拆分建议**：
  - 将所有 I/O 文件删除、粉碎覆写移出 UI 层，统一托管至专门的服务层 `DiskIoService` 或 `SecureFileEraser` 异步后台执行。

---

## 03. src/ui/CategoryModel.cpp :: CategoryModel

- **状态**：待处理
- **判定类型**：2.3 (数据层与业务层混杂)
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **职责清单（穷举当前承担的所有职责）**：
  1. 提供左侧分类面板的树形代理模型（QAbstractItemModel 实现）。
  2. **数据源反向耦合**：在 `refresh()` 期间，模型自发引入并直接调用了 `CategoryRepo` 仓储方法拉取系统和用户分类的条数统计及实体。
- **代码证据**：`CategoryModel::refresh`。
```cpp
// 源码行号：120 - 128
void CategoryModel::refresh() {
    beginResetModel();
    // 数据反灌：模型类直接调用底层仓储查询最新条数
    auto sysCounts = CategoryRepo::getSystemCounts();
    auto catCountsVec = CategoryRepo::getCounts();
```
- **拆分建议**：
  - 掐断 Model 层向底层的反向引用，应由 Controller 级查询数据后，一键单向注入 Model，使其重新专注于数据呈现媒介角色。

---

## 04. src/ui/TagManagerView.cpp :: TagManagerView

- **状态**：待处理
- **判定类型**：2.3 (数据层与业务层混杂)
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **职责清单（穷举当前承担的所有职责）**：
  1. 标签大屏视图的排版和子控件交互。
  2. **直接接管多线程线程池和底层数据库仓储**：在增删改分组标签时，在 View 内部直接调用 `QtConcurrent::run` 后台线程池包裹 `TagRepository` 写入数据库，再抛回主线程。
- **代码证据**：`TagManagerView::renameGroup`。
```cpp
// 源码行号：355 - 361
void TagManagerView::renameGroup(int groupId, const QString& newName) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, groupId, newName]() {
        if (TagRepository::renameGroup(groupId, newName)) {
```
- **拆分建议**：
  - 移出 UI 内的多线程并发与仓储层调用，将其解耦至中介 `TagManagerController` 控制器层处理。

---

## 本次排查与分析范围说明

- **已核实并直接读取源码（A级）的文件清单**：
  - `src/ui/CategoryPanel.cpp`
  - `src/ui/ContentPanel.cpp`
  - `src/ui/CategoryModel.cpp`
  - `src/ui/TagManagerView.cpp`
- **尚未验证的假设**：
  - 假定在将 UI 层的多线程调度提取到 Controller 层时，现有的 UI 状态同步槽不会在极高并发下触发竞态死锁（需在物理重构时详细测试线程安全性）。
