#include "CategoryLibraryPanel.h"
#include "UiHelper.h"
#include "../core/ModelContract.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include "../util/AssetImporter.h"
#include "../util/ShellHelper.h"
#include <QVBoxLayout>
#include <QMenu>
#include <QHeaderView>
#include <QApplication>
#include <QCoreApplication>

namespace ArcMeta {

CategoryLibraryPanel::CategoryLibraryPanel(QWidget* parent) : QFrame(parent) {
    m_model = new ArcMetaVirtualDbModel(this);
    m_proxyModel = new FilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    initUi();

    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setInterval(150);
    m_visibleTimer->setSingleShot(true);
    connect(m_visibleTimer, &QTimer::timeout, this, &CategoryLibraryPanel::refreshVisibleThumbnails);
}

CategoryLibraryPanel::~CategoryLibraryPanel() {}

void CategoryLibraryPanel::initUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_viewStack = new QStackedWidget(this);
    layout->addWidget(m_viewStack);

    m_gridView = new QListView(this);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setUniformItemSizes(true);
    m_gridView->setDragEnabled(true);
    m_gridView->setAcceptDrops(true);
    m_gridView->setDropIndicatorShown(true);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridView->setModel(m_proxyModel);
    m_viewStack->addWidget(m_gridView);

    m_treeView = new QTreeView(this);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setModel(m_proxyModel);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setUniformRowHeights(true);
    m_viewStack->addWidget(m_treeView);

    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &CategoryLibraryPanel::onSelectionChanged);
    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &CategoryLibraryPanel::onDoubleClicked);
    connect(m_gridView, &QAbstractItemView::customContextMenuRequested, this, &CategoryLibraryPanel::onCustomContextMenuRequested);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &CategoryLibraryPanel::onSelectionChanged);
    connect(m_treeView, &QAbstractItemView::doubleClicked, this, &CategoryLibraryPanel::onDoubleClicked);
    connect(m_treeView, &QAbstractItemView::customContextMenuRequested, this, &CategoryLibraryPanel::onCustomContextMenuRequested);

    m_viewStack->setCurrentIndex(0);
    updateGridSize();
}

void CategoryLibraryPanel::setViewMode(int mode) {
    m_viewStack->setCurrentIndex(mode == 0 ? 0 : 1);
}

void CategoryLibraryPanel::setZoomLevel(int level) {
    m_zoomLevel = level;
    updateGridSize();
}

void CategoryLibraryPanel::updateGridSize() {
    m_gridView->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
    m_gridView->setGridSize(QSize(m_zoomLevel + 16, m_zoomLevel + 28));
}

void CategoryLibraryPanel::applyFilters(const FilterState& state) {
    m_proxyModel->currentFilter = state;
    m_proxyModel->updateFilter();
}

QModelIndexList CategoryLibraryPanel::getSelectedIndexes() const {
    if (m_viewStack->currentIndex() == 0) {
        return m_gridView->selectionModel()->selectedIndexes();
    }
    return m_treeView->selectionModel()->selectedIndexes();
}

void CategoryLibraryPanel::onSelectionChanged() {
    QItemSelectionModel* selectionModel = (m_viewStack->currentIndex() == 0) ? m_gridView->selectionModel() : m_treeView->selectionModel();
    if (!selectionModel) return;

    QStringList selectedPaths;
    QModelIndexList indices = selectionModel->selectedIndexes();
    for (const QModelIndex& index : indices) {
        if (index.column() == 0) {
            QString path = m_proxyModel->mapToSource(index).data(PathRole).toString();
            if (!path.isEmpty()) selectedPaths.append(path);
        }
    }
    emit selectionChanged(selectedPaths);
}

void CategoryLibraryPanel::onDoubleClicked(const QModelIndex& index) {
    QModelIndex srcIdx = m_proxyModel->mapToSource(index);
    QString path = srcIdx.data(PathRole).toString();
    bool isCategory = srcIdx.data(IsCategoryRole).toBool();
    int catId = srcIdx.data(CategoryIdRole).toInt();

    if (isCategory && catId != -1) {
        emit categoryClicked(catId);
    } else {
        ShellHelper::openItem(path);
    }
}

void CategoryLibraryPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QWidget* view = qobject_cast<QWidget*>(sender());
    if (!view) return;

    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);

    QModelIndex index = (view == m_gridView) ? m_gridView->indexAt(pos) : m_treeView->indexAt(pos);
    bool onItem = index.isValid();

    if (onItem) {
        QModelIndex srcIdx = m_proxyModel->mapToSource(index);
        QString path = srcIdx.data(PathRole).toString();

        menu.addAction("打开", [path]() { ShellHelper::openItem(path); });
        menu.addSeparator();

        QMenu* categorizeMenu = menu.addMenu("归类到...");
        UiHelper::applyMenuStyle(categorizeMenu);

        auto categories = CategoryRepo::getAll(); // Use getAll() or getRecentlyUsed()

        for (const auto& cat : categories) {
            categorizeMenu->addAction(QString::fromStdWString(cat.name), [path, cat]() {
                CategoryRepo::associateItem(cat.id, path.toStdWString());
            });
        }
    }

    menu.exec(view->mapToGlobal(pos));
}

void CategoryLibraryPanel::refreshAll() {
    if (m_currentCategoryId != -1) {
        loadCategory(m_currentCategoryId);
    } else if (!m_loadedPaths.isEmpty()) {
        loadPaths(m_loadedPaths);
    }
}

void CategoryLibraryPanel::loadCategory(int categoryId) {
    m_currentCategoryId = categoryId;
    m_loadedPaths.clear();

    auto folderIds = CategoryRepo::getItemFolderIds(categoryId);
    std::vector<ItemRecord> records;
    for (const auto& fid : folderIds) {
        std::wstring wpath = MetadataManager::instance().getPathByFolderId(fid);
        if (!wpath.empty()) {
            records.push_back(ItemRecord::create(QString::fromStdWString(wpath)));
        }
    }

    m_model->setRecords(records);
    m_proxyModel->sort(0, Qt::AscendingOrder);
    refreshVisibleThumbnails();
}

void CategoryLibraryPanel::loadPaths(const QStringList& paths) {
    m_currentCategoryId = -1;
    m_loadedPaths = paths;

    std::vector<ItemRecord> records;
    for (const auto& path : paths) {
        records.push_back(ItemRecord::create(path));
    }

    m_model->setRecords(records);
    m_proxyModel->sort(0, Qt::AscendingOrder);
    refreshVisibleThumbnails();
}

void CategoryLibraryPanel::refreshVisibleThumbnails() {
    QAbstractItemView* view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_gridView) : static_cast<QAbstractItemView*>(m_treeView);
    if (!view) return;

    QRect viewportRect = view->viewport()->rect();
    QList<int> visibleRows;
    int rc = m_proxyModel->rowCount();
    for (int i = 0; i < rc; ++i) {
        QModelIndex proxyIdx = m_proxyModel->index(i, 0);
        QRect r = view->visualRect(proxyIdx);
        if (r.intersects(viewportRect)) {
            QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
            visibleRows.append(srcIdx.row());
        }
    }

    if (!visibleRows.isEmpty()) {
        m_model->loadThumbnailsForRows(visibleRows);
    }
}

} // namespace ArcMeta
