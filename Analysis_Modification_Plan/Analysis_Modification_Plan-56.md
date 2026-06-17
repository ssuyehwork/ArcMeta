# 统一导航历史记录系统重构方案 —— Analysis_Modification_Plan-56.md

## 1. 现状分析 (Problem Analysis)

### 1.1 历史记录系统的局限性
目前的导航历史记录（Forward/Back）存在严重的逻辑断层，无法提供连贯的用户体验：
- **物理路径孤岛**：`MainWindow::m_history` 目前仅被定义为 `QStringList`，且仅在执行物理磁盘跳转（`navigateTo`）时才会被记录。
- **逻辑跳转遗漏**：所有通过侧边栏点击触发的“分类跳转”（如点击自定义分类、全部数据、回收站等）均通过 `loadCategory` 或 `loadPaths` 直接操作 `ContentPanel`，完全跳过了历史记录的入栈逻辑。
- **后果**：用户无法通过“后退”按钮在物理文件夹和逻辑分类之间自由切换。例如：从 `C:\Downloads` 点击侧边栏的“红标文件”，再点击“后退”，系统无法返回 `C:\Downloads`，或者直接返回到了更早之前的物理路径，逻辑链条发生断裂。

---

## 2. 需求对照表 (Mandatory Comparison Table)

| 编号 | 用户原话 / 理解 | 我的方案对应点 | 是否一致 |
| :--- | :--- | :--- | :--- |
| 1 | “前进/后退”希望能按照顺序记录打开过的内容 | 确立“全场景记录”原则，将物理跳转与逻辑跳转统一序列化。 | 是 |
| 2 | 无论打开数据库某个分类还是从“目录导航”打开文件夹 | 拦截 CategoryPanel 与 NavPanel 的所有导航信号，汇聚至统一入口。 | 是 |
| 3 | 按照顺序记录，以便通过前进/后退还原状态 | 实现状态感知型的调度器，支持根据历史记录类型自动还原 UI 选中状态与数据内容。 | 是 |

---

## 3. 解决方案设计 (Proposed Solution)

### 3.1 统一导航状态建模 (Unified Navigation State)
不再单纯存储 `QString` 路径，而是采用“虚拟协议路径”或结构化对象来描述导航状态：
- **物理模式**：`file://C:/Work`
- **分类模式**：`category://{id}?name={display_name}`
- **系统模式**：`system://all` | `system://trash` | `system://recently_visited`

### 3.2 建立核心调度中枢 (Centralized Dispatcher)
在 `MainWindow` 中重写 `navigateTo` 逻辑，使其成为全系统唯一的导航驱动源：

1.  **收拢入口**：将 `m_categoryPanel` 的 `categorySelected` 信号和 `m_navPanel` 的 `directorySelected` 信号全部重定向至 `unifiedNavigateTo`。
2.  **分流执行**：
    -   解析协议头。
    -   如果是 `file://`：驱动 `ContentPanel->loadDirectory`，同步 `NavPanel` 选中态，更新 `AddressBar`。
    -   如果是 `category://`：驱动 `ContentPanel->loadCategory`，同步 `CategoryPanel` 选中态，更新 `AddressBar` 文本。
3.  **原子化压栈**：在执行加载前，判定 `record` 标志位，确保前进/后退操作本身不再重复压栈。

### 3.3 状态还原增强
- **侧边栏同步**：在执行后退操作时，不仅要刷新内容区，还必须通过 `m_categoryPanel->selectCategory(id)` 或 `m_navPanel->selectPath(path)` 物理同步侧边栏的高亮状态，确保 UI 表现与数据内容物理一致。

---

## 4. 修改文件列表 (Affected Files)

- `src/ui/MainWindow.h`：将 `m_history` 修改为支持复杂状态记录的容器，定义导航协议解析逻辑。
- `src/ui/MainWindow.cpp`：重构 `initUi` 中的信号槽连接，实现 `unifiedNavigateTo` 核心调度器。

---

## 5. 潜在风险 (Potential Risks)
- **无效状态还原**：当用户后退到某个历史分类，但该分类已被删除时，需在调度器中增加“存在性校验”，若校验失败应自动跳过该历史项或回退至“此电脑”。
- **性能开销**：协议解析与状态同步涉及多个面板联动，需确保逻辑轻量化，避免在连续点击后退时产生 UI 卡顿。
