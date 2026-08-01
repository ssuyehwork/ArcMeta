#include "DiskExplorerPanel.h"
#include "UiHelper.h"
#include "../util/ShellHelper.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
#include "models/ArcMetaVirtualDbModel.h"
#include "MediaColorExtractor.h"
#include "../meta/AmMetaJson.h"
#include <QVBoxLayout>
#include <QMenu>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>

namespace ArcMeta {

DiskExplorerPanel::DiskExplorerPanel(QWidget* parent) : QFrame(parent) {
    m_model = new ArcMetaVirtualDbModel(this);
    m_proxyModel = new FilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    initUi();

    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setInterval(150);
    m_visibleTimer->setSingleShot(true);
    connect(m_visibleTimer, &QTimer::timeout, this, &DiskExplorerPanel::refreshVisibleThumbnails);
}

DiskExplorerPanel::~DiskExplorerPanel() {}

void DiskExplorerPanel::initUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_viewStack = new QStackedWidget(this);
    layout->addWidget(m_viewStack);

    // Grid View
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

    // Tree View
    m_treeView = new QTreeView(this);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setModel(m_proxyModel);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setUniformRowHeights(true);
    m_viewStack->addWidget(m_treeView);

    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &DiskExplorerPanel::onSelectionChanged);
    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &DiskExplorerPanel::onDoubleClicked);
    connect(m_gridView, &QAbstractItemView::customContextMenuRequested, this, &DiskExplorerPanel::onCustomContextMenuRequested);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &DiskExplorerPanel::onSelectionChanged);
    connect(m_treeView, &QAbstractItemView::doubleClicked, this, &DiskExplorerPanel::onDoubleClicked);
    connect(m_treeView, &QAbstractItemView::customContextMenuRequested, this, &DiskExplorerPanel::onCustomContextMenuRequested);

    m_viewStack->setCurrentIndex(0);
    updateGridSize();
}

void DiskExplorerPanel::setViewMode(int mode) {
    m_viewStack->setCurrentIndex(mode == 0 ? 0 : 1);
}

void DiskExplorerPanel::setZoomLevel(int level) {
    m_zoomLevel = level;
    updateGridSize();
}

void DiskExplorerPanel::updateGridSize() {
    m_gridView->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
    m_gridView->setGridSize(QSize(m_zoomLevel + 16, m_zoomLevel + 28));
}

void DiskExplorerPanel::applyFilters(const FilterState& state) {
    m_proxyModel->currentFilter = state;
    m_proxyModel->updateFilter();
}

QModelIndexList DiskExplorerPanel::getSelectedIndexes() const {
    if (m_viewStack->currentIndex() == 0) {
        return m_gridView->selectionModel()->selectedIndexes();
    }
    return m_treeView->selectionModel()->selectedIndexes();
}

void DiskExplorerPanel::onSelectionChanged() {
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

void DiskExplorerPanel::onDoubleClicked(const QModelIndex& index) {
    QModelIndex srcIdx = m_proxyModel->mapToSource(index);
    QString path = srcIdx.data(PathRole).toString();
    bool isDir = srcIdx.data(IsDirRole).toBool();

    if (isDir) {
        emit directorySelected(path);
    }
}

void DiskExplorerPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QWidget* view = qobject_cast<QWidget*>(sender());
    if (!view) return;

    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);

    QModelIndex index = (view == m_gridView) ? m_gridView->indexAt(pos) : m_treeView->indexAt(pos);
    bool onItem = index.isValid();

    if (onItem) {
        QString path = m_proxyModel->mapToSource(index).data(PathRole).toString();
        menu.addAction("打开", [path]() { ShellHelper::openItem(path); });
        menu.addAction("在资源管理器中显示", [path]() { ShellHelper::showInExplorer(path); });
        menu.addSeparator();
        menu.addAction("物理删除", [this, path]() {
            if (ShellHelper::deleteItem(path)) {
                refreshAll();
            }
        });
    } else {
        menu.addAction("新建文件夹", [this]() {
            QDir dir(m_currentPath);
            QString newDir = dir.absoluteFilePath("新建文件夹");
            if (QDir().mkdir(newDir)) {
                refreshAll();
            }
        });
    }

    menu.exec(view->mapToGlobal(pos));
}

void DiskExplorerPanel::refreshAll() {
    if (!m_currentPath.isEmpty()) {
        loadDirectory(m_currentPath, m_isRecursive);
    }
}

void DiskExplorerPanel::loadDirectory(const QString& path, bool recursive) {
    m_currentPath = path;
    m_isRecursive = recursive;

    int reqId = ++m_loadRequestId;
    QPointer<DiskExplorerPanel> weakThis(this);

    (void)QtConcurrent::run([weakThis, path, recursive, reqId]() {
        std::vector<ItemRecord> allItems;

        std::function<void(const QString&, bool)> scanDir;
        scanDir = [&](const QString& p, bool rec) {
            QDir dir(p);
            if (!dir.exists()) return;

            // 专属 AmMetaJson 离散打标载入
            AmMetaJson jsonCache(p.toStdWString());
            jsonCache.load();
            const auto& cachedItems = jsonCache.items();

            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
            for (const QFileInfo& info : entries) {
                if (!weakThis) return;
                if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue;
                if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;

                QString absPath = info.absoluteFilePath();
                ItemRecord itemRec = ItemRecord::create(absPath);

                std::wstring fileName = info.fileName().toStdWString();
                auto it = cachedItems.find(fileName);
                if (it != cachedItems.end()) {
                    itemRec.rating = it->second.rating;
                    itemRec.manualColor = QString::fromStdWString(it->second.color);
                    itemRec.pinned = it->second.pinned;
                    itemRec.note = QString::fromStdWString(it->second.note);
                    itemRec.url = QString::fromStdWString(it->second.url);
                    itemRec.tags.clear();
                    for (const auto& t : it->second.tags) {
                        itemRec.tags.append(QString::fromStdWString(t));
                    }
                }

                allItems.push_back(itemRec);

                if (rec && info.isDir()) {
                    scanDir(absPath, true);
                }
            }
        };

        scanDir(path, recursive);

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, allItems, reqId]() {
            if (weakThis && weakThis->m_loadRequestId == reqId) {
                weakThis->m_model->setRecords(allItems);
                weakThis->m_proxyModel->sort(0, Qt::AscendingOrder);
                weakThis->refreshVisibleThumbnails();
            }
        });
    });
}

void DiskExplorerPanel::refreshVisibleThumbnails() {
    QAbstractItemView* view = (m_viewStack->currentIndex() == 0) ? m_gridView : m_treeView;
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
