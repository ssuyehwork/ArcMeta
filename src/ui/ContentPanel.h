#pragma once

#include <QFrame>
#include <QStackedWidget>
#include "DiskExplorerPanel.h"
#include "CategoryLibraryPanel.h"

namespace ArcMeta {

class ContentPanel : public QFrame {
    Q_OBJECT
public:
    explicit ContentPanel(QWidget* parent = nullptr);
    ~ContentPanel() override;

    enum ViewMode {
        GridView,
        ListView
    };

    void deferredInit();
    void setViewMode(ViewMode mode);
    ViewMode currentViewMode() const { return m_currentViewMode; }

    QAbstractItemModel* model() const;
    QSortFilterProxyModel* getProxyModel() const;
    QModelIndexList getSelectedIndexes() const;

    bool isMirrorSource() const;

public slots:
    void setZoomLevel(int level);
    void loadDirectory(const QString& path, bool recursive = false);
    void loadCategory(int categoryId);
    void loadPaths(const QStringList& paths);
    void refreshAll();
    void applyFilters(const FilterState& state);

signals:
    void zoomLevelChanged(int level);
    void viewModeChanged(ViewMode mode);
    void selectionChanged(const QStringList& paths);
    void directorySelected(const QString& path);
    void categoryClicked(int categoryId);
    void directoryStatsReady(
        const QMap<int, int>&     ratingCounts,
        const QMap<QString, int>& colorCounts,
        const QMap<QString, int>& typeCounts,
        const QMap<QString, int>& createDateCounts,
        const QMap<QString, int>& modifyDateCounts,
        int emptyFolderCount);

private:
    QStackedWidget* m_containerStack = nullptr;
    DiskExplorerPanel* m_diskPanel = nullptr;
    CategoryLibraryPanel* m_categoryPanel = nullptr;
    ViewMode m_currentViewMode = GridView;
    int m_zoomLevel = 64;
};

} // namespace ArcMeta
