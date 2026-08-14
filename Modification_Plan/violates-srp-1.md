# 职责不单一文件深度排查与分析 —— violates-srp-1.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 桌面客户端的架构迭代中，虽然此前对“双轨路由隔离”、“万级拖拽性能”、“操作快照”等核心机制进行了多次细节升级与对账优化，但随着软件规模的持续增长，UI 视图层与底层数据持久层、物理 I/O 操作之间仍残留着较为明显的“高耦合、职责错位”架构债务。
本方案旨在通过对整个 `src/` 主干源码进行深度、客观的代码级排查，找出不满足单一职责原则（Single Responsibility Principle, SRP）及层级边界隔离规范的代码文件，记录其具体的多重职责清单、事实引用，并给出对应的物理拆分解耦设计，从而为后续的系统级高内聚整洁架构治理提供权威的图纸依据。

## 2. 问题定位
通过深度静态代码扫描与实际调用链追踪，共精确定位到 4 处不满足“单一职责原则 (SRP)”的核心代码文件。其核心根因在于 UI 视图层（QWidget 及其子类）过度越权，直接接管了数据库 API 调用、多线程后台调度以及文件系统的物理删除动作。

具体不合规的文件清单如下：
1. **`src/ui/CategoryPanel.cpp`**：UI 界面层直接内联了 SQLite 的原生 C-API 进行数据写操作、事务提交和复杂的排序盘点，严重违反了 N-Tier 数据层与表现层严格隔离的底线。
2. **`src/ui/ContentPanel.cpp`**：中心主数据视口作为 UI 呈现者，现场执行了递归式的物理文件/文件夹删除、三遍扇区随机覆写安全粉碎，并直接调用数据库层操作，属于 God Object 职责过载。
3. **`src/ui/CategoryModel.cpp`**：作为视图模型层（QAbstractItemModel 的子类），在重构树节点数据时自发向底层的 CategoryRepo 仓储层拉取数据进行装配，违背了“模型仅用作展现，数据装配应由上游 Controller 统驭并注入”的原则。
4. **`src/ui/TagManagerView.cpp`**：作为一个纯粹承载标签管理的 QWidget 排版 UI 控件，在添加/删除/重命名标签组的操作中直接启动 `QtConcurrent::run` 线程，并跨线程调用 TagRepository，导致 UI 层与后台并发、数据库仓储层直接越级耦合。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：排查代码中不符合单一职责原则（SRP）的文件，并记录分析结果。 | 本方案核心事件名：职责不单一文件深度排查与分析 —— violates-srp-1.md | ✅ 一致 |
| 1    | 先去分析当前版本的代码文件，哪些代码文件存在职责不单一的先记载到Violates SRP.md里 | 精确定位 4 处核心不合规的代码文件，并在本方案中作为物理铁证链归档记录 | ✅ 一致 |
| 2    | 物理文件名：严禁使用中文或包含空格等非英文字符。升级版命名机制：必须新建文件，保留完整的历史追踪链路（如 violates-srp-1.md）。 | 新建文件名符合英文小写及连字符自解释命名 `violates-srp-1.md`，旧文件原样留存 | ✅ 一致 |

---

## 4. 详细解决方案

以下排查发现均基于当前最新的主干源码。针对每一处职责不单一的核心债务，均给出具体的事实认定、确定性分级以及详细的拆分解耦方案。

---

### 4.1 `src/ui/CategoryPanel.cpp` :: CategoryPanel 【确定性：A级 — 已核实，提供真实行号代码证据】

- **判定类型**：2.1 (God Object) 及 2.3 (数据层与业务层混杂)
- **职责清单（穷举当前承担的所有职责）**：
  1. 侧边栏分类面板组件的视觉组装，包括自定义分类树、系统预置逻辑桶、快速访问镜像代理节点的展示。
  2. 自定义分类文件夹展开/折叠（双态指示箭头）、行内编辑（ElasticEdit）创建时的交互。
  3. 鼠标右键动作响应（如排序、重命名、批量删除、密码锁定与清空）。
  4. **底层原生数据库 API 操作**：在重排、移入回收站或修改分类等槽函数中，直接调用底层 sqlite3 原生 C-API 进行语句编译、绑定以及步进执行。
- **代码证据**：`CategoryPanel::reorderAll` 方法。UI 内部直接越权，充当底层数据库驱动，使用 sqlite3 C API 物理重排。
```cpp
// 源码行号：1075 - 1104
void CategoryPanel::reorderAll(int parentId) {
    auto db = DatabaseManager::instance().connection().memDb;
    if (!db) return;

    // 2026-06-xx 物理修复：在 UI 线程直接执行 sqlite3 事务与预编译语句
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE categories SET sort_order = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        qWarning() << "[CategoryPanel] Failed to prepare reorder SQL:" << sqlite3_errmsg(db);
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    int sortOrder = 0;
    // ... 对树节点排序进行逐一 UPDATE 物理写入 ...
    sqlite3_exec(db, "COMMIT TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);
}
```
- **拆分与解耦方案**：
  - **移出原生 SQL / 事务**：彻底从 `CategoryPanel` 剥离任何与原生 sqlite3 库（如 `sqlite3_stmt` 等 C API 结构）以及底层事务控制相关的语句。
  - **沉淀至仓储层 (CategoryRepo)**：将所有物理数据库重排序、重排版的事务操作封装为 `CategoryRepo::reorderAllInTransaction(int parentId, const QList<QPair<int, int>>& idWithOrders)`，UI 层仅组织数据并通过参数转发，由 Repository 专职负责底层数据写盘，解耦 UI 与 C API。

---

### 4.2 `src/ui/ContentPanel.cpp` :: ContentPanel 【确定性：A级 — 已核实，提供真实行号代码证据】

- **判定类型**：2.1 (God Object) 职责过载 及 2.3 (数据层与业务层混杂)
- **职责清单（穷举当前承担的所有职责）**：
  1. 内容呈现主视口的组装，切换并管理三种视图呈现态（List/Grid/Justified）。
  2. 处理多维筛选器及排序改变、重构本地 FilterProxyModel 逻辑（`applyFilters`）。
  3. **越权执行磁盘物理 I/O 与文件粉碎**：在 `onCustomContextMenuRequested`（第 2200 行起）在响应菜单动作时，直接在主线程阻塞执行高能耗、高风险的磁盘物理 recursive 删除以及随机数覆写（shredFile 安全粉碎）。
  4. 拖拽行为的命中测试、拖入、拖出接收，并直接通过 `CategoryRepo::addItemToCategory` 执行分类数据库绑定。
- **代码证据**：`ContentPanel` 中的彻底递归删除与安全粉碎写操作。
```cpp
// 源码行号：2230 - 2253
                            physicalOk = SecureFileEraser::shredFile(p);
                        } else {
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
                            physicalOk = recursiveRemove(p);
                        }
```
- **拆分与解耦方案**：
  - **抽离物理文件系统删除操作**：将 UI 层自写的递归删除和粉碎覆写逻辑彻底物理删除。
  - **托管至独立的服务层 (DiskIoService)**：调用 `DiskIoService::asyncDeletePaths(targetPaths, isSecure, ...)`，由 Service 将耗时的磁盘扫描、I/O 阻塞写入 QtConcurrent 后台子线程执行，并通过信号或回调将进度与成功状态传回 UI。主线程 UI 仅负责展示转圈等待（ProgressDialog），杜绝假死。

---

### 4.3 `src/ui/CategoryModel.cpp` :: CategoryModel 【确定性：A级 — 已核实，提供真实行号代码证据】

- **判定类型**：2.3 (数据层与业务层混杂)
- **职责清单（穷举当前承担的所有职责）**：
  1. 为 `CategoryPanel` 侧边栏视图提供树形代理数据结构（实现 QAbstractItemModel）。
  2. 包装并输出对应单元格节点的 QStandardItem 图标与样式文字（“名称 (N)”）。
  3. **数据源层级倒灌**：在 `refresh()` 期间，模型自发、直接地引入并调用了 `CategoryRepo::getSystemCounts()`、`CategoryRepo::getAll()` 以及 `CategoryRepo::getCounts()` 来获取最新的持久层分类数据和计数存根。
- **代码证据**：`CategoryModel::refresh` 中直接提取仓储层方法。
```cpp
// 源码行号：120 - 145
void CategoryModel::refresh() {
    beginResetModel();
    // ...
    // 数据倒灌：模型类直接调用仓储层获取全量分类并盘点计数
    auto sysCounts = CategoryRepo::getSystemCounts();
    auto catCountsVec = CategoryRepo::getCounts();
    QMap<int, int> catCounts;
    for (const auto& entry : catCountsVec) {
        catCounts[entry.first] = entry.second;
    }
    // ...
    endResetModel();
}
```
- **拆分与解耦方案**：
  - **回归数据呈现媒介本色**：移除 `CategoryModel` 内部任何对 `CategoryRepo` 仓储层的头文件包含与直接函数调用。
  - **推崇单向依赖流 (Controller 驱动)**：应由 `CategoryPanel` 或 `CategoryUiController` 在外部查询好对应的分类实体（`QList<CategoryEntry>`）及最新计数快照，通过强契约接口 `m_categoryModel->setCategories(data, stats)` 显式注入模型。模型只负责按注入的数据进行 QStandardItem 生成和刷新，彻底掐断数据层向模型层的“自下而上”反向耦合。

---

### 4.4 `src/ui/TagManagerView.cpp` :: TagManagerView 【确定性：A级 — 已核实，提供真实行号代码证据】

- **判定类型**：2.3 (数据层与业务层混杂)
- **职责清单（穷举当前承担的所有职责）**：
  1. 管理标签管理大屏的侧边分组栏、常用标签栏及主面板组件排版展示。
  2. 捕捉并响应用户点击按钮、右键快捷菜单。
  3. **越权执行数据调度与异步后台计算**：在添加、删除或重命名标签分类组时，直接自行调用了 `QtConcurrent::run` 并引入底层 `TagRepository` 写入数据库，写完后再抛回主线程 `invokeMethod` 刷新。
- **代码证据**：`TagManagerView` 直接操控并发任务与 TagRepository。
```cpp
// 源码行号：355 - 378
void TagManagerView::renameGroup(int groupId, const QString& newName) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, groupId, newName]() {
        if (TagRepository::renameGroup(groupId, newName)) {
            if (weakThis) QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
        }
    });
}
```
- **拆分与解耦方案**：
  - **剔除多线程后台并发与仓储层调用**：剥离 `TagManagerView` 内的 `QtConcurrent::run` 以及对 `TagRepository` 静态类方法的直接调用。
  - **建立 Controller 契约分流**：`TagManagerView` 通过成员引用 `TagManagerController* m_controller`。当触发右键动作时，仅向控制器发送语义信号：`m_controller->requestRenameGroup(groupId, newName)`。由 Controller 调度后台并发线程、读写 `TagRepository` 并在完成后回调刷新 UI，使 View 真正纯净化。

---

## 5. 修改边界声明【范围】

本文件仅作为**不改变任何物理代码、不进行任何代码编译**的静态排查、分析与重构规划设计图。
在进行后续的执行与物理拆分实施时，其物理作用边界限制在以下声明的范围中，绝对不可越界修改相邻逻辑：

**本次方案涉及范围：**
- [ ] `src/ui/CategoryPanel.cpp` — 移出 `sqlite3_*` 原生 API 及直接的重排序大事务。
- [ ] `src/ui/ContentPanel.cpp` — 移出 `shredFile` 和递归物理删除的文件系统 I/O 操作。
- [ ] `src/ui/CategoryModel.cpp` — 移出对 `CategoryRepo` 的包含与数据提取。
- [ ] `src/ui/TagManagerView.cpp` — 移出 `QtConcurrent::run` 及对 `TagRepository` 的跨线程直接调用。

**明确禁止越界修改的范围：**
- [ ] `src/meta/sqlite3.c` 与 `src/meta/sqlite3.h` — 保持原始数据库内核实现，不作任何物理触碰与改动。
- [ ] 视图层的多态布局组件（`JustifiedView`、`GridView` 等）及基础样式渲染细节 — 保持不变，不触碰具体 UI 绘图引擎。

---

## 6. 实现准则与预警【核心】

1. **头文件严格解耦**：在执行拆分时，应仔细检查这四个 UI 类，彻底移除 `#include <sqlite3.h>`、`#include "../meta/CategoryRepo.h"`、`#include "../meta/TagRepository.h"` 等层级错位的持久层引用，防止后续任何人在 UI 层直接“随手”调用 SQL 查询。
2. **多线程调用与弱引用（QPointer）安全预警**：
   在把并发写库和物理 I/O 操作下放到 Controller/Service 层并配合 `QtConcurrent::run` 异步执行时，必须严格遵守 Qt 跨线程安全准则：
   - 严禁在后台 worker 线程直接读写或操作任何 QWidget/QStandardItem/QModelIndex 视觉组件，这属于绝对非线程安全操作，会导致进程瞬间崩溃。
   - 在 worker 线程回调主线程刷新 UI 前，必须通过 `QPointer` 弱引用检查上下文组件（如 `weakThis`）是否在异步执行期间已被析构关闭（防止由于快速关闭窗口引发的 UAF（Use After Free）野指针崩溃）。
3. **彻底消灭编译器未引用警告**：
   重构时一旦解耦或合并了部分无用参数，需同步物理清除其局部声明行；对于重写虚接口（如 `eventFilter`）不需要引用的形参，需在签名中将其注销（例如：`QObject* /*watched*/`），杜绝任何编译器 `-Wunused-variable` / `-Wunused-parameter` 警告。

---

## 7. Memories.md 合规检查

以下为本项目核心偏好对齐，针对本排查分析进行了严密的合规核准：

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| **输入框清除按钮** | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，且只可采用“Qt 原生的 setClearButtonEnabled(true)”，杜绝脑补另创。 | ✅ 符合。本分析未涉及输入框修改，后续重构时也将无任何自定义清除按钮，保持此准则完全不受损害。 |
| **异步加载防闪烁** | 异步数据扫描前禁止先行调用 clear()，避免数据空窗期引起黑白屏视觉抖动，应毫秒级原子替换。 | ✅ 符合。在将物理文件/数据库盘点异步拆分时，各加载服务（如 CategoryLoadService）必须先保持当前 Model 数据，等异步读取成功后通过 setRecords 进行原子更新。 |
| **双轨数据路由** | isManagedContext() 判定：在托管库内 100% 写入统一 SQLite 数据库；在普通磁盘下调用 AmMetaJson 写入 ArcMeta.cache。 | ✅ 符合。将 ContentPanel 和 CategoryPanel 中的数据变更重构拆分时，均严格根据 isManagedContext() 数据源契约，路由分发具体的写存储模式，绝对禁止磁盘导航模式写操作倒灌入 SQLite 本地库。 |

---

## 8. 待确认事项（可选）

1. **关于 `CategoryModel::refresh()` 重构的数据注入方式**：
   建议通过 `CategoryUiController` 或 `CategoryPanel` 统一充当调度中介。当收到 `__RELOAD_ALL__` 信号时，由 Panel/Controller 从 `CategoryRepo` 与 `MetadataManager` 中提取最新的逻辑分类数据和系统原子统计快照，将整理好的结构化实体喂给模型。此设计最为高内聚，是否批准这一路径？
2. **关于 `TagManagerView` 是否需要增加标准的 `TagManagerController` 控制器骨架**：
   排查发现 `TagManagerController` 的原型类（仅 25 行，见 srp_scanner 的结果）实际上已经建立在 `src/ui/TagManagerController.cpp` 中。我们建议将 `TagManagerView` 中的所有并发后台写库调度完整移植到 `TagManagerController` 对应方法下，将二者真正连通。是否批准这一具体连通解耦动作？
