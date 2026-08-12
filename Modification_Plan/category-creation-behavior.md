# 侧边栏选中分类时新建资产与分类的归属绑定逻辑 —— category-creation-behavior.md

> 状态：已执行完成（纯机制分析，无须修改代码）

## 1. 任务背景
在 ArcMeta 系统中，用户在侧边栏选中某个自定义分类（例如分类“A”）后，在主内容面板（ContentPanel）空白处通过右键菜单执行“新建 ➔ 文件夹 / Markdown / 纯文本文件”操作。
本方案针对这一交互逻辑进行理论和代码层面的深度对账与机制说明，确认在该场景下：
1. 创建的“新建文件夹”（逻辑子分类）是否自动归属于当前选中分类“A”的子分类。
2. 创建的“新建 Markdown / TXT”（虚拟资产文件）是否自动归属于当前选中分类“A”。

## 2. 问题定位
当前代码的物理逻辑分布于以下三个核心模块中：
- `src/ui/ContentPanel.cpp` 中的 `ContentPanel::createNewItem(const QString& type)` 方法。
- `src/ui/MainWindow.cpp` 中的信号槽联动绑定。
- `src/ui/CategoryModel.cpp` 中关于分类层级父子关系的呈现与统计更新。

通过对上述源码的审计，确认系统已在架构设计中 100% 完美支撑并实现了上述行为。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：侧边栏选中分类时，内容面板创建的虚拟分类及文件的归属绑定逻辑。 | 本方案核心事件名：侧边栏选中分类时新建资产与分类的归属绑定逻辑 | ✅ |
| 1    | 选中分类“A”，在内容面板创建的虚拟分类应该归属于“A”的子分类（对应用户原话：“假设用户选中了侧边栏分类“A”，此时在内容面板创建的虚拟分类是不是应该归属于“A”的子分类呢？”） | `ContentPanel::createNewItem` 触发 `emit requestCreateSubCategory(m_currentCategoryId);`，通知侧边栏在 “A” 下创建逻辑子分类。 | ✅ |
| 2    | 创建的文件也应该归属（绑定）到“A”分类（对应用户原话：“创建的文件也应该归属（绑定）到“A”分类呢？”） | `ContentPanel::createNewItem` 注册文件时将 `m_currentCategoryId` 作为绑定分类参数传递，写入 SQLite `category_items` 表。 | ✅ |

## 4. 详细解决方案
由于代码中已经 100% 实现了该功能逻辑，本节将通过剖析已有的底层源码，证明其物理和逻辑实现。

### 4.1 新建虚拟分类（文件夹）的归属逻辑
在 `src/ui/ContentPanel.cpp` 中：
```cpp
void ContentPanel::createNewItem(const QString& type) { 
    ...
    // --- 分流 B：内存受控托管库模式 (UserCategory) ---
    if (m_currentCategoryId <= 0) return;

    // 场景 B1：新建文件夹 ➔ 生成逻辑子分类
    if (type == "folder") {
        // 向侧边栏发射请求，由 CategoryPanel 自动展开并直接进入行内编辑重命名
        emit requestCreateSubCategory(m_currentCategoryId);
        return;
    }
    ...
}
```
- **解释**：当用户选中分类 “A” 时，`m_currentCategoryId` 存储的就是分类 “A” 的数据库 ID（即 `m_currentCategoryId > 0`）。
- 此时点击“创建文件夹”，内容面板发射 `requestCreateSubCategory` 信号，并将分类 “A” 的 ID 传递出去。
- 在 `src/ui/MainWindow.cpp` 中，该信号与侧边栏进行了绑定：
```cpp
    connect(m_contentPanel, &ContentPanel::requestCreateSubCategory, this, [this](int parentCatId) {
        if (m_categoryPanel) {
            m_categoryPanel->selectCategory(parentCatId);
            m_categoryPanel->onCreateSubCategory();
        }
    });
```
- 主窗口接收到信号后，自动先将侧边栏选中定位到分类 “A”，然后调用侧边栏的 `onCreateSubCategory()`。
- `onCreateSubCategory()` 会自动以当前选中的分类（此时即为 “A”）作为 `parentId`，在 `categories` 表中创建一条新的子分类记录（例如“新建分类”），并且直接在侧边栏分类树对应节点下将其展开并进入行内编辑重命名状态。
- **结论**：**创建的虚拟分类完全归属于“A”的子分类，完美实现父子关联。**

### 4.2 新建虚拟文件的归属逻辑
在 `src/ui/ContentPanel.cpp` 中，当新建 Markdown 或 TXT 时：
```cpp
void ContentPanel::createNewItem(const QString& type) { 
    ...
    // 场景 B2：新建 Markdown / txt ➔ 在托管库建立 Base36 胶囊物理文件并绑定分类
    QString baseName = "未命名";
    QString ext = (type == "md") ? ".md" : ".txt";
    QString fileName = baseName + ext;
    ...
    // 2. 分配 13 位 Base36 胶囊 ID 并物理创建文件
    QString fileId = ShellHelper::generateBase36Id();
    QString containerDir = managedRoot + "/" + fileId + ".arc";
    if (!QDir().mkpath(containerDir)) return;

    QString destPath = containerDir + "/" + fileName;
    ...
    // 3. 登记写入 SQLite 数据库并绑定至当前分类 ID
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString();
    if (MetadataManager::instance().registerAsset(fileId.toStdString(), wDestPath, m_currentCategoryId)) {
        // 4. 定位高亮并进入行内编辑状态
        m_pendingSelectName = fileName;
        m_isPendingEdit = true;
        refreshAll();
    }
}
```
- **解释**：在创建了物理胶囊文件后，程序立即同步调用 `MetadataManager::registerAsset`，并把当前的 `m_currentCategoryId`（即 “A” 的分类 ID）直接作为参数传入。
- `registerAsset` 会在底层将该文件的哈希、路径等元数据入库，同时在分类关联表 `category_items` 中，将该资产与当前分类 ID “A” 绑定。
- **结论**：**创建的文件 100% 自动归属（绑定）到分类“A”中。**

## 5. 修改边界声明【范围】
本案为纯机制审计，不改变现有高健壮性的双轨代码。

**本次方案涉及范围：**
- [ ] 模块/文件：无（代码已完美实现）

**明确禁止越界修改的范围：**
- [ ] 现有双轨逻辑——不修改。

## 6. 实现准则与预警【核心】
- **机制保障**：现有代码已通过强类型的信号槽联动机制，消除了物理磁盘冗余空目录的生成，并在 SQLite 层面和 QStandardItemModel 层面确保了数据一致性。
- **交互自愈**：通过 `requestCreateSubCategory` 信号打通了内容面板和侧边栏分类树的行内编辑，操作极其丝滑，且对 parent 分类没有任何破坏性风险。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨路由物理隔离机制 | 托管库模式下逻辑处理，磁盘导航模式下物理处理。本机制完全对齐这一要求。 | ✅ |
| 行内编辑新建逻辑子分类联动规范 | 新建文件夹动作自动转化为在 SQLite 数据库中创建指向当前分类的“逻辑子分类”节点，不生成冗余空目录。 | ✅ |

## 8. 待确认事项（可选）
无。
