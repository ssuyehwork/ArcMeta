#pragma once

#include <QFrame>
#include <QStackedWidget>
#include <QTreeView>
#include <QListView>
#include <QTimer>
#include <QModelIndex>
#include "models/ArcMetaVirtualDbModel.h"
#include "models/FilterProxyModel.h"

namespace ArcMeta {

class CategoryLibraryPanel : public QFrame {
    Q_OBJECT
public:
    explicit CategoryLibraryPanel(QWidget* parent = nullptr);
    ~CategoryLibraryPanel() override;

    void loadCategory(int categoryId);
    void loadPaths(const QStringList& paths);
    void setViewMode(int mode);
    void setZoomLevel(int level);
    void applyFilters(const FilterState& state);
    void refreshAll();

    ArcMetaVirtualDbModel* model() const { return m_model; }
    FilterProxyModel* proxyModel() const { return m_proxyModel; }
    QModelIndexList getSelectedIndexes() const;

signals:
    void selectionChanged(const QStringList& paths);
    void categoryClicked(int categoryId);

private slots:
    void onSelectionChanged();
    void onDoubleClicked(const QModelIndex& index);
    void onCustomContextMenuRequested(const QPoint& pos);
    void refreshVisibleThumbnails();

private:
    void initUi();
    void updateGridSize();

    QStackedWidget* m_viewStack = nullptr;
    QListView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    ArcMetaVirtualDbModel* m_model = nullptr;
    FilterProxyModel* m_proxyModel = nullptr;

    int m_currentCategoryId = -1;
    QStringList m_loadedPaths;
    int m_zoomLevel = 64;
    QTimer* m_visibleTimer = nullptr;
};

} // namespace ArcMeta
