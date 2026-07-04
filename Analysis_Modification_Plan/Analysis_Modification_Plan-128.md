# Analysis_Modification_Plan-128: “失效数据”监督机制与逻辑分流实施方案

## 1. 任务背景
ArcMeta 需要增强托管库（ArcMeta.Library_[盘符]）的数据完整性校验。当托管库内文件被第三方应用（如 Windows 资源管理器）删除时，系统应将其标记为“失效”并保留元数据供审计；当由 ArcMeta 内部执行“移出托管库”操作时，则执行物理抹除。同时，优化 `renameItem` 存在的性能与逻辑缺陷。

## 2. 问题定位
- **状态分流缺失**：当前 `onEntryRemoved` 无法区分操作来源（内部 vs 外部）。
- **失效标记未物化**：虽然 `metadata` 表有 `is_invalid` 字段，但未在删除路径中有效利用。
- **UI 联动不足**：点击“失效数据”时，导航面板未按需求收敛。
- **renameItem 缺陷**：
    1.  扩展名倒排索引更新 bug（指向错误容器）。
    2.  缺乏事务保护，导致大规模移动时 I/O 阻塞。

## 3. 强制对照表（引用用户需求）
| 用户原话 | 实现方案 |
| :--- | :--- |
| “判定'失效'的唯一触发条件是...被外部应用真实删除” | 在 `onEntryRemoved` 中检查 `internalOperating == false`。 |
| “ArcMeta 应用内部主动执行...直接从数据库彻底抹除该记录” | 在 `onEntryRemoved` 中检查 `internalOperating == true`，执行 `removeMetadataSync`。 |
| “点击'失效数据'这个分类节点时...面板应当收敛隐藏” | 在 `unifiedNavigateTo` 切换系统协议 `invalid_data` 时执行隐藏逻辑。 |
| “新增值 -2 表示'已失效'...如果你审查后发现现有 ingestion_status 的语义与'失效'存在冲突...可以提出改为独立 is_invalid 字段” | **审计结论**：复用独立 `is_invalid` 字段（证据：`DatabaseManager.cpp:124`）。 |
| “每条记录存完整绝对路径字符串...需要新增一段批量前缀替换逻辑” | **审计结论**：`path` 存完整绝对路径（证据：`DatabaseManager.cpp:115`）。在 `renameItem` 中实现级联路径更新。 |

## 4. 详细解决方案

### 4.1 数据库与逻辑层 (MetadataManager)
- **修复 `renameItem` (src/meta/MetadataManager.cpp)**:
    - **逻辑 A（扩展名索引）**：修正 `m_extensionToFids` 的 `push_back` 目标容器。
    - **逻辑 B（路径级联）**：在持有写锁期间，通过 `itemsToRename` 集合，计算所有受影响的子项新路径。
    - **逻辑 C（异步事务化）**：
        1.  将整个更名逻辑移入 `QtConcurrent::run`。
        2.  内存库：使用 `SqlTransaction` 包裹循环。
        3.  磁盘库：收集所有 `(fid, newPath)` 对，通过 `enqueueSyncTask` 发送单个任务，内部执行一次大事务提交。
- **增强失效设置**:
    - 确保 `setInvalidByFrn` 能够通过 `(frn, volSerial)` 准确定位。

### 4.2 信号审计分流 (AutoImportManager)
- **修改 `onEntryRemoved` (src/core/AutoImportManager.cpp)**:
    - 获取 `internalOperating` 状态。
    - 若 `true`：调用 `MetadataManager::instance().removeMetadataSync(path)`。
    - 若 `false`：调用 `MetadataManager::instance().setInvalidByFrn(frn, volSerial, true)`。

### 4.3 UI 交互彻底收敛与新列表组件 (MainWindow)
- **四面板彻底隐藏**：修改 `MainWindow::unifiedNavigateTo`，当协议为 `system://invalid_data` 时，**强制隐藏**以下四个面板：
    1.  `m_navPanel` (目录导航)
    2.  `m_contentPanel` (主内容网格)
    3.  `m_metaPanel` (元数据)
    4.  `m_filterPanel` (筛选)
- **独立扁平列表视图 (`InvalidDataListView`)**：
    - 在 `m_mainSplitter` 中新增一个独立的 `QTreeView` 实例。
    - **列定义**：
        - 第 0 列：复选框 (支持全选)
        - 续：文件名、原始路径 (`original_path`)、失效时间 (`mtime`)、文件类型、大小。
    - **交互与批量删除**：
        - 顶部工具栏提供“全选/取消全选”与“彻底删除”按钮。
        - “彻底删除”需弹出二次确认，确认后调用 `MetadataManager::removeMetadataBatchSync`。
- **状态恢复**：从失效视图切换出时，通过 `loadPanelVisibility()` 恢复用户自定义的面板布局。

### 4.4 数据层 Bug 修复与批量操作优化 (MetadataManager & CategoryRepo)
- **高性能批量删除 (`removeMetadataBatchSync`)**：
    - **理由**：独立于单条删除逻辑，以支持单一内存事务与单一异步磁盘事务任务，避免 N 次 IO 导致的假死。
    - **实现**：
        1.  持有 `m_mutex` 写锁，一次性从 `m_cache` 和倒排索引中移除所有目标路径。
        2.  内存库：包裹在一个 `SqlTransaction` 内。
        3.  磁盘库：投递一个包含所有 `file_id` 的 Lambda 任务至 `enqueueSyncTask`，内部执行 `BEGIN/COMMIT` 物理删除。
- **修复查询不一致**：修改 `CategoryRepo::getSystemCategoryPaths`，确保失效数据查询优先使用 `original_path` 兜底。

## 5. 修改边界
- `src/meta/MetadataManager.cpp`: 重构 `renameItem` 内部逻辑。
- `src/core/AutoImportManager.cpp`: 修改 `onEntryRemoved` 触发分支。
- `src/ui/MainWindow.cpp`: 实现彻底的面板收敛与 `InvalidDataListView` 切换。
- `src/meta/CategoryRepo.cpp`: 修复失效数据查询不一致 Bug。

## 6. 实现准则与预警
- **原子性**：级联更名必须在 SQL 事务内完成。
- **线程安全**：`renameItem` 异步化后，必须确保在进入异步前已通过 `normalizePath` 处理完所有对 Qt 对象的依赖，或确保信号发射回到 GUI 线程。
- **状态一致性**：移出托管库的“硬删除”必须清理 `category_items`（`removeMetadataSync` 已涵盖）。

## 7. Memories.md 合规检查
- [x] 符合“托管库入库 = 物理位移”原则：物理变动驱动失效标记。
- [x] 符合“移动即抹除”规约：库内向库外移动（内部操作）执行硬删除。
- [x] 符合“QtConcurrent::run 编译优化规约”：使用 `(void)` 忽略返回值。
