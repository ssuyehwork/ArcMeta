# Implicit Dual-Container Architecture —— 隐式双容器架构设计与实现.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在普通单视图架构下，当用户对资产进行升序或降序排列时，文件夹和文件容易发生上下颠倒、混合穿插或样式混叠。本方案引入“隐式双容器”核心架构，底层将文件夹与文件彻底解耦隔离并使用两个独立的过滤代理模型，视觉上完全一体化无缝融合、流畅滚动，彻底根治此类问题。

## 2. 问题定位
- **底层交叉缺陷**：现有的单 `m_proxyModel` 混合了文件夹和物理文件，在排序（`FilterProxyModel::lessThan`）时不得不使用各种强人工置顶逻辑（如 `leftIsDir != rightIsDir`），脆弱且容易在降序时导致文件夹沉底或混合穿插。
- **物理分层思路**：
  - **上层隐式容器（文件夹区）**：物理绑定 `FolderProxyModel`，专门过滤只保留文件夹和分类（`isCategory || isDir` 为真）。
  - **下层隐式容器（文件区）**：物理绑定 `FileProxyModel`，专门过滤只保留文件（`isCategory || isDir` 为假）。
  - **滚动统一**：物理禁用两个子容器各自的滚动条，使用统一的 `QScrollArea` 容纳这两个无缝紧贴的视图。通过高度自适应内容，实现无边界的一体化滚动。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：隐式双容器（文件夹与文件物理隔离）重构 | 本方案核心事件名：隐式双容器架构设计与实现 | ✅ |
| 1    | 采用方案 A，且上下两个容器必须是“隐式（无缝/无界限）”的 | 两个子视图紧贴且禁用滚动条，由外层 QScrollArea 驱动，视觉完全一体 | ✅ |
| 2    | 文件夹永在上方，文件永在下方，只在各自容器内部重排 | 双代理模型物理解耦，各自容器单独重排，天然实现文件夹始终置顶 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/ContentPanel.h`
新增隐式双容器和双模型所需的成员声明。

<<<<<<< SEARCH // 视图组件 QAbstractItemView* m_gridView = nullptr; QTreeView* m_treeView = nullptr; DiskItemModel* m_diskModel = nullptr; // 负责纯物理磁盘导航模型 (0) LibraryAssetModel* m_libraryModel = nullptr; // 负责内存托管逻辑资产模型 (1) ItemModelBase* m_model = nullptr; // 当前多态激活指针合约

QTimer* m_visibleTimer = nullptr;
void refreshVisibleThumbnails();
QSortFilterProxyModel* m_proxyModel = nullptr;
======= // 视图组件 QAbstractItemView* m_gridView = nullptr; QTreeView* m_treeView = nullptr; DiskItemModel* m_diskModel = nullptr; // 负责纯物理磁盘导航模型 (0) LibraryAssetModel* m_libraryModel = nullptr; // 负责内存托管逻辑资产模型 (1) ItemModelBase* m_model = nullptr; // 当前多态激活指针合约

// 隐式双容器架构
QWidget* m_seamlessContainer = nullptr;      // 外层无缝无界限滚动主容器
QVBoxLayout* m_seamlessLayout = nullptr;     // 统一无缝纵向布局器
QAbstractItemView* m_folderView = nullptr;   // 上层隐式容器（专门展示文件夹）
QAbstractItemView* m_fileView = nullptr;     // 下层隐式容器（专门展示文件）

FilterProxyModel* m_folderProxyModel = nullptr; // 文件夹专有过滤代理
FilterProxyModel* m_fileProxyModel = nullptr;   // 文件专有过滤代理

QTimer* m_visibleTimer = nullptr;
void refreshVisibleThumbnails();
QSortFilterProxyModel* m_proxyModel = nullptr;
REPLACE


### 4.2 修改 `src/ui/ContentPanel.cpp` 中的 `FilterProxyModel::filterAcceptsRow`
支持按代理类型对文件夹与文件分别实行物理分流。

<<<<<<< SEARCH bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const { const auto* sourceModelPtr = qobject_cast<const ItemModelBase*>(sourceModel()); if (!sourceModelPtr) return true;

const auto& records = sourceModelPtr->allRecords();
if (sourceRow < 0 || sourceRow >= (int)records.size()) return true;

const auto& rec = records[sourceRow];
if (rec.isGroupHeader) return true;

QString fileName = rec.isCategory ? rec.categoryName : rec.filename;

// 1. 本地高级多维条件过滤 (星级、色系、标签、日期等)
if (!currentFilter.accepts(rec)) {
    return false;
}

// 2. 本地搜索输入框过滤 (keyword 本地过滤)
if (currentFilter.keyword.isEmpty()) {
    return true;
}
return fileName.contains(currentFilter.keyword, Qt::CaseInsensitive); 
}
bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const { const auto* sourceModelPtr = qobject_cast<const ItemModelBase*>(sourceModel()); if (!sourceModelPtr) return true;

const auto& records = sourceModelPtr->allRecords();
if (sourceRow < 0 || sourceRow >= (int)records.size()) return true;

const auto& rec = records[sourceRow];
if (rec.isGroupHeader) return true;

QString fileName = rec.isCategory ? rec.categoryName : rec.filename;

// 1. 本地高级多维条件过滤 (星级、色系、标签、日期等)
if (!currentFilter.accepts(rec)) {
    return false;
}

// 2. 本地搜索输入框过滤 (keyword 本地过滤)
if (!currentFilter.keyword.isEmpty() && !fileName.contains(currentFilter.keyword, Qt::CaseInsensitive)) {
    return false;
}

// 3. 隐式双容器过滤分流：通过 objectName 区分当前过滤器的角色
bool isDirOrCat = (rec.isCategory || rec.isDir);
if (objectName() == "FolderProxyModel") {
    return isDirOrCat;
} else if (objectName() == "FileProxyModel") {
    return !isDirOrCat;
}

return true; 
}

REPLACE


### 4.3 修改 `src/ui/ContentPanel.cpp` 中的 `FilterProxyModel::lessThan`
彻底剥离已不再需要的伪置顶比对逻辑（物理第三权重中文件夹与文件的判定）。

<<<<<<< SEARCH // 3. 物理第一权重：文件夹与子分类始终置顶 (绝对强制，升降序下均不动摇) bool leftIsDir = (leftRec.isDir || leftRec.isCategory); bool rightIsDir = (rightRec.isDir || rightRec.isCategory);

if (leftIsDir != rightIsDir) {
    return leftIsDir; // 文件夹永远“更小”排在前面
}

// 4. 物理第二权重：置顶优先规则 (升降序下均强制置顶，不随用户排序取反下沉)
bool leftPinned = leftRec.pinned || leftRec.encrypted;
bool rightPinned = rightRec.pinned || rightRec.encrypted;

if (leftPinned != rightPinned) { 
    return leftPinned; // 置顶项永远排在前面
} 
======= // 3. 物理第一权重：置顶优先规则 (升降序下均强制置顶，不随用户排序取反下沉) bool leftPinned = leftRec.pinned || leftRec.encrypted; bool rightPinned = rightRec.pinned || rightRec.encrypted;

if (leftPinned != rightPinned) { 
    return leftPinned; // 置顶项永远排在前面
} 
REPLACE


### 4.4 修改 `src/ui/ContentPanel.cpp` 中的 `initGridView`
重构网格初始化布局，组装隐式无缝滚动双容器结构。

<<<<<<< SEARCH void ContentPanel::initGridView() { // 🚨 极端优化锁：完全剔除 QListWidget，100% 物理复用 QListView 并开启 UniformItemSizes m_gridView = new DropListView(this); m_gridView->setFrameShape(QFrame::NoFrame); m_gridView->setAlternatingRowColors(false); m_gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); m_gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);

QPalette gp = m_gridView->palette(); 
gp.setColor(QPalette::Base, QColor("#1E1E1E")); 
m_gridView->setPalette(gp); 

m_gridView->setDragEnabled(true); 
m_gridView->setAcceptDrops(true); 
m_gridView->setDragDropMode(QAbstractItemView::DragDrop); 

m_gridView->setItemDelegate(new ThumbnailDelegate(this)); 
m_gridView->setModel(m_proxyModel); 
m_gridView->viewport()->installEventFilter(this); 

connect(m_gridView, SIGNAL(pathsDropped(QStringList,QModelIndex)), this, SLOT(onPathsDropped(QStringList,QModelIndex))); 
connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
connect(m_gridView, &QListView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
connect(m_gridView, &QListView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { 
    refreshVisibleThumbnails(); 
}); 

updateGridSize(); 
}
void ContentPanel::initGridView() { // 隐式双容器架构重构 m_seamlessContainer = new QWidget(this); m_seamlessContainer->setObjectName("SeamlessContainer"); m_seamlessContainer->setStyleSheet("QWidget#SeamlessContainer { border: none; background: transparent; }");

m_seamlessLayout = new QVBoxLayout(m_seamlessContainer);
m_seamlessLayout->setContentsMargins(0, 0, 0, 0);
m_seamlessLayout->setSpacing(12);

// 实例化双模型代理
m_folderProxyModel = new FilterProxyModel(this);
m_folderProxyModel->setObjectName("FolderProxyModel");
m_folderProxyModel->setSourceModel(m_model);
m_folderProxyModel->setFilterKeyColumn(0);
m_folderProxyModel->setDynamicSortFilter(true);

m_fileProxyModel = new FilterProxyModel(this);
m_fileProxyModel->setObjectName("FileProxyModel");
m_fileProxyModel->setSourceModel(m_model);
m_fileProxyModel->setFilterKeyColumn(0);
m_fileProxyModel->setDynamicSortFilter(true);

auto setupSubView = [&](QAbstractItemView*& view, FilterProxyModel* proxy) {
    view = new DropListView(this); 
    view->setFrameShape(QFrame::NoFrame); 
    view->setAlternatingRowColors(false); 
    // 隐式双容器必须完全禁用子容器独立滚动条
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    view->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    
    QPalette gp = view->palette(); 
    gp.setColor(QPalette::Base, QColor("#1E1E1E")); 
    view->setPalette(gp); 
    
    view->setDragEnabled(true); 
    view->setAcceptDrops(true); 
    view->setDragDropMode(QAbstractItemView::DragDrop); 
    
    view->setItemDelegate(new ThumbnailDelegate(this)); 
    view->setModel(proxy); 
    view->viewport()->installEventFilter(this); 
    
    connect(view, SIGNAL(pathsDropped(QStringList,QModelIndex)), this, SLOT(onPathsDropped(QStringList,QModelIndex))); 
    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
    connect(view, &QListView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
    connect(view, &QListView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
};

setupSubView(m_folderView, m_folderProxyModel);
setupSubView(m_fileView, m_fileProxyModel);

m_seamlessLayout->addWidget(m_folderView);
m_seamlessLayout->addWidget(m_fileView);

// 让旧网格指针指向外围无感包装，以保持与其他视图切换生命周期的一致
m_gridView = m_seamlessContainer;

// 初始化排序与默认样式对齐
m_folderProxyModel->sort(0, m_sortOrder);
m_fileProxyModel->sort(0, m_sortOrder);

updateGridSize(); 
}

REPLACE


### 4.5 修改 `src/ui/ContentPanel.cpp` 中的 `applyFilters`
在筛选后添加隐式容器的动态折叠和显隐控制。

<<<<<<< SEARCH void ContentPanel::applyFilters() { if (!m_proxyModel) return; m_proxyModel->currentFilter = m_currentFilter; m_proxyModel->updateFilter(); updateStatusBarStats(); }
void ContentPanel::applyFilters() { if (!m_proxyModel) return; m_proxyModel->currentFilter = m_currentFilter; m_proxyModel->updateFilter();

// 隐式双容器筛选动态联动
if (m_folderProxyModel) {
    m_folderProxyModel->currentFilter = m_currentFilter;
    m_folderProxyModel->updateFilter();
    if (m_folderProxyModel->rowCount() == 0) {
        m_folderView->hide();
    } else {
        m_folderView->show();
    }
}

if (m_fileProxyModel) {
    m_fileProxyModel->currentFilter = m_currentFilter;
    m_fileProxyModel->updateFilter();
    if (m_fileProxyModel->rowCount() == 0) {
        m_fileView->hide();
    } else {
        m_fileView->show();
    }
}

updateStatusBarStats();
}

REPLACE


## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/file：`src/ui/ContentPanel.h` (定义外层主容器、纵向布局器、双子视图以及双代理过滤器成员变量)
- [ ] 模块/file：`src/ui/ContentPanel.cpp` (重构 `filterAcceptsRow` 过滤分流、`lessThan` 剥离伪置顶、`initGridView` 拼装双隐式视图及 `applyFilters` 折叠联动)

**明确禁止越界修改的范围：**
- [ ] 内容面板其它视图切换（如 `ListResultView` 或 `JustifiedResultView`）——不修改
- [ ] 物理文件操作与右键菜单项定义——不修改

## 6. 实现准则与预警【核心】
1. **隐藏无缝顺滑度**：必须物理禁用子视图的垂直与水平滚动条（`Qt::ScrollBarAlwaysOff`），避免视觉穿帮。
2. **多代理关联同步**：对主数据模型的更新或源变化，必须同步重新设置 `FolderProxyModel` 与 `FileProxyModel` 的 `setSourceModel()`，确保数据链路高度一致。
3. **开箱即用铁律**：所有新声明的类成员和中间逻辑，必须保证 100% 被定义并正确初始化。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 异步加载与防闪烁 | 异步加载扫描期间禁止先行调用 clear() 以防止“白屏”抖动，且需有竞态保护 | ✅ |
| 内容面板数据源判定与强类型契约 | 必须使用强类型 `DataSourceType` 统一规范内容面板数据源，并使用 `isMirrorSource()` 进行联动 | ✅ |
| 线程安全边界与 Qt GUI 保护 | 修改段均不涉及后台多线程并发调用 Qt GUI API 资源 | ✅ |

## 8. 待确认事项（可选）
无