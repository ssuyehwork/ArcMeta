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

class DiskExplorerPanel : public QFrame {
    Q_OBJECT
public:
    explicit DiskExplorerPanel(QWidget* parent = nullptr);
    ~DiskExplorerPanel() override;

    void loadDirectory(const QString& path, bool recursive = false);
    void setViewMode(int mode);
    void setZoomLevel(int level);
    void applyFilters(const FilterState& state);
    void refreshAll();

    ArcMetaVirtualDbModel* model() const { return m_model; }
    FilterProxyModel* proxyModel() const { return m_proxyModel; }
    QModelIndexList getSelectedIndexes() const;

signals:
    void selectionChanged(const QStringList& paths);
    void directorySelected(const QString& path);
    void directoryStatsReady(
        const QMap<int, int>&     ratingCounts,
        const QMap<QString, int>& colorCounts,
        const QMap<QString, int>& typeCounts,
        const QMap<QString, int>& createDateCounts,
        const QMap<QString, int>& modifyDateCounts,
        int emptyFolderCount);

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

    QString m_currentPath;
    bool m_isRecursive = false;
    int m_zoomLevel = 64;
    QTimer* m_visibleTimer = nullptr;
    std::atomic<int> m_loadRequestId{0};
};

} // namespace ArcMeta
