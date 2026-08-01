#include "ContentPanel.h"
#include <QVBoxLayout>

namespace ArcMeta {

ContentPanel::ContentPanel(QWidget* parent) : QFrame(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_containerStack = new QStackedWidget(this);
    layout->addWidget(m_containerStack);

    m_diskPanel = new DiskExplorerPanel(this);
    m_categoryPanel = new CategoryLibraryPanel(this);

    m_containerStack->addWidget(m_diskPanel);      // Index 0
    m_containerStack->addWidget(m_categoryPanel);  // Index 1

    connect(m_diskPanel, &DiskExplorerPanel::selectionChanged, this, &ContentPanel::selectionChanged);
    connect(m_diskPanel, &DiskExplorerPanel::directorySelected, this, &ContentPanel::directorySelected);
    connect(m_diskPanel, &DiskExplorerPanel::directoryStatsReady, this, &ContentPanel::directoryStatsReady);

    connect(m_categoryPanel, &CategoryLibraryPanel::selectionChanged, this, &ContentPanel::selectionChanged);
    connect(m_categoryPanel, &CategoryLibraryPanel::categoryClicked, this, &ContentPanel::categoryClicked);

    m_containerStack->setCurrentIndex(0);
}

ContentPanel::~ContentPanel() {}

void ContentPanel::deferredInit() {}

void ContentPanel::setViewMode(ViewMode mode) {
    m_currentViewMode = mode;
    int intMode = (mode == GridView) ? 0 : 1;
    m_diskPanel->setViewMode(intMode);
    m_categoryPanel->setViewMode(intMode);
    emit viewModeChanged(mode);
}

void ContentPanel::setZoomLevel(int level) {
    m_zoomLevel = level;
    m_diskPanel->setZoomLevel(level);
    m_categoryPanel->setZoomLevel(level);
    emit zoomLevelChanged(level);
}

QAbstractItemModel* ContentPanel::model() const {
    if (m_containerStack->currentIndex() == 0) {
        return m_diskPanel->model();
    }
    return m_categoryPanel->model();
}

QSortFilterProxyModel* ContentPanel::getProxyModel() const {
    if (m_containerStack->currentIndex() == 0) {
        return m_diskPanel->proxyModel();
    }
    return m_categoryPanel->proxyModel();
}

QModelIndexList ContentPanel::getSelectedIndexes() const {
    if (m_containerStack->currentIndex() == 0) {
        return m_diskPanel->getSelectedIndexes();
    }
    return m_categoryPanel->getSelectedIndexes();
}

bool ContentPanel::isMirrorSource() const {
    return m_containerStack->currentIndex() == 1;
}

void ContentPanel::loadDirectory(const QString& path, bool recursive) {
    m_containerStack->setCurrentIndex(0);
    m_diskPanel->loadDirectory(path, recursive);
}

void ContentPanel::loadCategory(int categoryId) {
    m_containerStack->setCurrentIndex(1);
    m_categoryPanel->loadCategory(categoryId);
}

void ContentPanel::loadPaths(const QStringList& paths) {
    m_containerStack->setCurrentIndex(1);
    m_categoryPanel->loadPaths(paths);
}

void ContentPanel::refreshAll() {
    if (m_containerStack->currentIndex() == 0) {
        m_diskPanel->refreshAll();
    } else {
        m_categoryPanel->refreshAll();
    }
}

void ContentPanel::applyFilters(const FilterState& state) {
    m_diskPanel->applyFilters(state);
    m_categoryPanel->applyFilters(state);
}

} // namespace ArcMeta
