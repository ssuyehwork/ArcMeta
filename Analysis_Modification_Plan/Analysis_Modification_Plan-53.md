# Analysis_Modification_Plan-53.md —— 目录导航机制分析与双模搜索方案

## 1. 现状分析 (Problem Analysis)

### 1.1 目录导航 (NavPanel) 运行机制
- **数据源**：完全基于 Windows 物理文件系统。
- **技术栈**：`QTreeView` + `QStandardItemModel` + `QDir`。
- **加载逻辑**：采用“懒加载”模式。初始化时仅加载盘符，只有当用户点击“展开”箭头时，才会触发 `fetchChildDirs` 异步读取该目录下的第一级子目录。
- **与数据库关系**：**无直接耦合**。`NavPanel` 本身不读取 `Arcmeta_*.db`。它的存在是为了让用户即使在没有录入数据库的情况下，也能以传统方式管理物理文件。

### 1.2 搜索失效原因
- **逻辑孤岛**：目前的搜索框（`MainWindow` -> `CoreController` -> `MetadataManager`）仅在内存缓存（`m_cache`）中进行关键词匹配。
- **断层**：由于“目录导航”中的文件只有在被选中、归类或修改元数据时才会进入 `m_cache`（通过 `registerItem`），因此那些仅“存在于磁盘但未被系统操作过”的文件对搜索框是不可见的。

---

## 2. 需求对照表 (Mandatory Comparison Table)

| 编号 | 用户原话/理解 | 我的方案对应点 | 是否一致 |
| :--- | :--- | :--- | :--- |
| 1 | “目录导航”是否使用到DB类数据库？如果不使用数据库，那么它又是怎么运行的？ | 详细说明 NavPanel 基于物理磁盘实时 IO 和懒加载机制，不依赖数据库。 | 是 |
| 2 | 既然动态读取，为什么搜索框搜索不到物理文件？ | 指出搜索逻辑目前仅受限于内存缓存（MetadataManager），未对物理路径执行扫描。 | 是 |
| 3 | 搜索框支持两种模式：数据库搜索 & 方案B（特定物理路径递归搜索） | 在 `CoreController` 引入物理递归搜索算法，根据 `MainWindow` 的活跃上下文自动切换模式。 | 是 |

---

## 3. 解决方案设计 (Proposed Solution)

### 3.1 上下文感知 (Context Awareness)
在 `MainWindow` 中强化 `m_lastDataSource` 的记录逻辑：
- 当用户点击“数据分类”面板项时，`m_lastDataSource` 设为 `"category"`。
- 当用户点击“目录导航”面板项时，`m_lastDataSource` 设为 `"nav"`，并记录当前选中的 `m_currentPath`。

### 3.2 核心搜索算法升级 (Search Engine Upgrade)
在 `CoreController::performSearch` 中实现分流：

1.  **数据库模式 (`lastDataSource == "category"`)**：
    -   继续使用 `MetadataManager::searchInCache(keyword, "")`。
    -   实现全局秒搜。

2.  **物理导航模式 (`lastDataSource == "nav"`)**：
    -   **输入参数**：`keyword` 和 `m_currentPath`。
    -   **执行逻辑**：
        -   在子线程启动 `QDirIterator(m_currentPath, QDirIterator::Subdirectories)`。
        -   实时遍历该物理目录下所有文件/文件夹。
        -   进行文件名模糊匹配。
        -   **元数据合并**：将搜到的物理路径去 `MetadataManager` 匹配，如果该文件已在数据库中有备注/标签，也纳入匹配范围。
    -   **性能保障**：由于物理扫描较慢，搜索结果通过信号分批推送给 `ContentPanel`，不阻塞 UI。

### 3.3 UI 反馈优化
- 当处于物理搜索模式时，状态栏显示“正在扫描物理路径...”提示。
- 在搜索结果中，通过 `ItemRecord` 的 `isManaged` 标记区分哪些是已录入项，哪些是纯物理项。

---

## 4. 修改文件列表 (Affected Files)

- `src/ui/MainWindow.cpp`：完善上下文记录，传递 `rootPath` 给搜索接口。
- `src/core/CoreController.cpp`：实现 `performSearch` 的物理扫描逻辑分流。
- `src/meta/MetadataManager.cpp`：优化缓存检索，支持与物理扫描结果的快速合并。

---

## 5. 潜在风险 (Potential Risks)

- **性能风险**：对于包含数十万文件的超大文件夹（如 `C:\Windows`），物理递归扫描会产生明显延迟。需设置超时保护或层级深度限制。
- **结果冗余**：需要确保物理搜索和缓存搜索结果去重。
