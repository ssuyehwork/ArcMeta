# ArcMeta 统一整改执行清单 (Master Remediation Roadmap)

本文档整合以下五份既有文档的全部发现，按"不改会出问题、改了才能稳定支撑
百万级数据规模"的优先级重新排序，作为后续所有重构工作的唯一执行依据：

- Overall Planning and Architecture.md（治理原则）
- PlanningandArchitecture.md（现状流程记录）
- ModularizationAudit.md（模块化专项审查）
- UiHelperAudit.md（UiHelper 专项审查）
- ArchitectureComplianceAudit.md（全量合规性审查，合规率 52.3%）

**前提数据规模**：数百万条记录级别，所有优先级判断均以此为基准，不采用
"数据量小、可忽略"的宽松标准。

**治理原则**：必须模块化、职责单一。宁愿承担重构成本，也不允许临时拼凑。
不满足职责单一、模块化的实现，一律视为不合格，无论其当前是否已经"能跑"。

---

## P0：立即修复（存在确定性死锁 / 数据损坏 / 系统性卡死风险，与其他改造无关，独立先行）

### P0-1. MetadataManager 自死锁
- **问题**：`forEachCachedItem`（持有 `shared_lock` 读锁）回调内部同步调用
  `setManaged`（申请 `unique_lock` 写锁），`shared_mutex` 不可重入，构成
  确定性死锁。
- **触发路径**：`CategoryRepo::fullRecount()` 等账本对账流程。
- **修复方向**：回调内部只收集"待处理路径列表"，读锁释放后在锁外批量
  执行写操作。

### P0-2. MetadataManager::searchInCache 全量扫描期间长时间持锁
- **问题**：百万级 `m_cache` 全量线性遍历，期间持有共享读锁，导致所有
  等待写锁的后台线程（MFT/USN 扫描等）被阻塞，用户日常搜索操作即可
  触发长达数秒至十几秒的系统性卡顿。
- **修复方向**：不能简单归为"缩短持锁时间"。需要重新设计检索机制——
  引入倒排索引/前缀索引，避免全量线性扫描；或改用分片锁降低锁粒度；
  搜索过程本身也应移出主线程同步调用链。请 Jules 先给出具体技术方案
  供确认，不要直接假设某种实现。

### P0-3. TagManagerView 硬编码 C 盘 + UI 直写 SQL
- **问题**：标签功能永久锁死在 C 盘分库，且 UI 控件类直接执行
  `sqlite3_prepare_v2`/`sqlite3_exec`。多盘符并发写入时会触发
  `SQLITE_BUSY`，导致界面闪退、数据回滚、标签库一致性被破坏。
- **修复方向**：标签数据访问收拢至统一的 `TagRepository`，明确标签的
  权威存储位置（是否应迁移至 global.db 而非绑定某个具体盘符），UI 层
  只通过 Repository 接口读写。

### P0-4. AutoImportManager 全局大锁 `s_dbAccessMutex`
- **问题**：所有盘符的对账、分类创建、数据库写入共用同一把递归锁，
  完全废掉了 WAL 并发和多驱动器独立并行的原始设计意图。
- **修复方向**：按盘符/按资源拆分锁粒度，互不相关的盘符扫描应能真正
  并行执行。

---

## P1：核心架构重建（内存模式恢复 + 计数机制重建，属于既定的架构性决策，非局部修补）

### P1-1. 恢复真正的 SQLite 内存模式
- 独立的 `:memory:` 连接（`conn.memDb`），不再等同于 `conn.diskDb`
- 内存↔磁盘同步一律使用 SQLite Backup API（页级整库拷贝），**严禁**
  逐行 SELECT+INSERT/UPDATE 循环同步（这是历史上"退出卡在 5940 步
  拷贝"问题的根因，必须明确规避）
- 15 秒定时异步落盘（通过 `enqueueSyncTask` 投递到工作线程，不阻塞
  主线程）
- 批量操作完成后追加一次即时异步落盘
- 退出流程只做一次基于 Backup API 的快速落盘，不设计任何"逐步计数"
  式的进度反馈
- **数据规模复核**：百万级数据对应的数据库文件体积可能达数百 MB 至
  GB 级，需要实测 Backup API 单次整库同步的实际耗时。如果耗时超出
  可接受范围，需评估引入增量同步机制（变更日志表 + 触发器）的必要性，
  不能想当然认为"页级拷贝天然够快"

### P1-2. 侧边栏计数机制：从"实时全量计算"改为"增量维护 + 持久化"
- `getSystemCounts()` 等禁止每次显示都遍历全量缓存实时统计
- 参照 `s_totalFileCount` 现有模式，为每个统计项维护内存原子变量，
  数据变更时增量更新并异步持久化到 `system_stats` 表
- 需要先完整列出所有影响计数的数据变更入口清单，确认覆盖完整后再
  实现，避免遗漏导致计数跑偏
- `recently_visited` 这类随时间变化的统计项需要单独处理方案（不能
  简单增量加减），需 Jules 明确给出方案
- `fullRecount()` 保留作为兜底校准，但只在启动完成后异步执行一次，
  不得阻塞主线程/首屏渲染，不再作为界面显示的实时数据来源

### P1-3. 主线程同步 I/O 阻塞点清理
- `GridItemDelegate::editorEvent` → `setRating` → `persistAsync`：
  确认内存模式改造后写入是否已降为内存速度；若仍有绕过 memDb 直接
  操作 diskDb 的路径需一并修正
- `CategoryModel::setData` 中的 `QFile::rename` 物理文件重命名操作
  移至后台线程执行，完成后异步回调更新 UI 与触发数据库写入
- `UiHelper::getFileIcon`（滚动时同步 Shell API 调用）：确认占位图标
  + 异步加载 + 局部 dataChanged 刷新的模式已生效
- `FilterProxyModel::lessThan` 排序比较函数中现场构造 `QFileInfo`：
  改为预先缓存排序所需字段

---

## P2：模块边界重建（消除耦合、统一持久层，解决"改一处牵连全身"的问题）

### P2-1. 建立统一 Repository 持久层
- 新建 `MetadataRepository`、`CategoryRepository`、`TagGroupRepository`
- 收拢当前散落在 `MetadataManager.cpp`、`CategoryRepo.cpp`、
  `TagManagerView.cpp` 三处的 SQL 语句、prepared statement 管理
- 业务层（`MetadataManager`、`CategoryRepo`、`TagManagerView`）只调用
  Repository 暴露的强类型接口，不再直接接触 `sqlite3_*` 系列函数
- 直接解决 P0-3 的架构根因，不只是"改掉硬编码 C 盘"这一个症状

### P2-2. 拆分 UiHelper（全能上帝类，扇出耦合面覆盖 8 个模块）
按 `UiHelperAudit.md` 给出的方案拆分为三个独立模块：
- **`SvgIconRenderer`**：纯 SVG 渲染，无外部依赖
  （承接 `renderIcon`/`getIcon`/`getPixmap`/`getSvgDataUrl` 等）
- **`WindowsShellThumbnailProvider`**：Shell COM 交互 + 磁盘缩略图缓存
  + 并发调度（承接 `getShellThumbnail`/`getFileIcon`，依赖
  `SvgIconRenderer` 生成 QSS 用临时 PNG）
- **`MediaColorExtractor`**：纯图像颜色分析算法，无状态、无 I/O
  （承接 `extractPalette`/`rgbToLab`/`calculateDeltaE` 等）
- **拆分风险提示**：`getFileIcon` 内的 `s_fileIconCache`、
  `s_loadingKeys`、`IconLoadNotifier` 单例是共享状态，拆分时需确保
  生命周期管理正确，避免引入新的线程亲和性问题或状态碎片化

### P2-3. 消除跨模块的直接内部访问（越权访问修复）
- `ContentPanel` 直接调用 `MetadataManager::getMeta()` 拉取内部
  `RuntimeMeta` 并自行拆解状态字段 → 改为通过更高层的业务呈现接口
- `CategoryModel::data()` 直接强转访问内部结构、直接调用
  `MetadataManager` 做锁判定 → 同上，收敛到接口
- `ThumbnailDelegate::setModelData()` 向上遍历父级组件树、强制类型
  转换找到 `ContentPanel` 并直接调用其方法 → 改为 Delegate 发出信号，
  由 `ContentPanel` 自行连接，不反向感知具体面板类型

### P2-4. CoreController 职责收敛
- `performSearch` 中直接执行 `QDirIterator` 物理磁盘全盘扫描的逻辑，
  与"全局中控管理系统状态"的职责不符，应剥离至独立的搜索服务模块

### P2-5. AutoImportManager 职责拆分
- 当前混合了"USN/IOCP 变化捕获"、"DFS 递归 1:1 分类创建"、"历史访问
  记录维护"三种职责，且三者被强同步串联在同一条调用链上（一环变慢，
  整个入库对账全部卡住）
- 拆分为独立的"物理探测"、"分类同步"、"历史记录"三个阶段，允许分阶段、
  可中断、可并行处理，而不是单向强同步链

---

## P3：低优先级（代码整洁度，不影响正确性和性能，可延后处理）

- `ThumbnailDelegate::paint()` 拆分为多个小函数（`drawThumbnail`/
  `drawStatusBadge`/`drawExtensionBadge` 等），提升可读性
- `UiHelper` 拆分后确认无遗留的空引用（如 `CategorySetPasswordDialog.cpp`
  中确认存在的多余 `#include "UiHelper.h"`）

---

## 执行铁律（贯穿以上所有阶段，不可违背）

1. **必须模块化、职责单一**。宁可承担重构成本，也不采取临时拼凑方案。
   任何新增代码如果导致某个类重新混入不相关职责，视为本次改造失败。
2. **内存↔磁盘同步一律使用 Backup API**，严禁任何形式的逐行循环同步。
3. **禁止在数据变更的显示路径上做全量遍历/全量计算**。凡是"统计类"
   需求，一律走"增量维护 + 持久化 + 读取缓存值"模式。
4. **UI 控件类不允许直接访问数据库底层 API**，一律通过 Repository。
5. **禁止跨模块直接访问内部实现细节**（越过公开接口的强制类型转换、
   遍历对象树查找具体类型等），一律通过信号槽或公开接口通信。
6. **禁止笼统的全局大锁**保护多个互不相关的资源。
7. 每个阶段改造完成后，**必须在百万级测试数据规模下验证**，不能只用
   小数据量测试通过就视为完成——本项目历次性能问题都源于"小数据量
   下测试正常，真实规模下才暴露"。
8. 每个阶段完成后同步更新 `PlanningandArchitecture.md`，确保文档与
   代码保持一致，不允许出现"文档描述的架构与实际代码不符"的情况
   （历史上 `getMemoryDb` 函数名与其实际磁盘直连实现不符，已造成过
   严重的认知混乱，引以为戒）。

---

## 建议执行顺序

P0（四项，独立并行修复，互不依赖）→ P1-1（内存模式，是 P1-2/P1-3
部分内容的前提）→ P1-2 与 P1-3（可并行）→ P2-1（Repository 层，是
P2-3 部分内容的基础）→ P2-2 与 P2-4 与 P2-5（可并行）→ P2-3 → P3。

每完成一个阶段，请 Jules 生成对应的改动摘要与百万级测试数据下的实测
结果，供确认后再进入下一阶段，不允许多阶段一次性混合提交。