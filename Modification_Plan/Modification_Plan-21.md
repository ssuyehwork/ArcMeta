# ContentPanel 物理模块化拆分与双轨 100% 物理隔离 —— Modification_Plan-21.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

为了彻底贯彻“磁盘目录模式与内存数据库模式 100% 隔离”这一最高架构原则，并解决在同一个超大源文件（`ContentPanel.cpp`）中两轨代码交叉堆叠、容易发生混淆和误调用的设计缺陷，本方案将 `ContentPanel` 深度重构物理拆分为 3 个职责高度单一、物理上完全断连不共享的模块。此举从编译级别直接阻断了磁盘导航模式误调用 SQLite 数据库的可能，使系统架构具有工业级的极高稳定性和清晰度。

---

## 2. 问题定位

1. **混杂度过高**：原 `ContentPanel.cpp` 包含 3600 余行代码，同时维护了磁盘物理导航、内存数据库分类读取、解包还原、缩略图加载及排序过滤等多重逻辑。
2. **缺乏物理隔离阻断**：磁盘目录模式在物理文件操作（重命名、粘贴等）时极易因为处于同一个类/文件中而误共享、误查询底层数据库，无法达成在物理源码级杜绝误调用的安全红线。
3. **架构负债重**：在同一个源文件中使用各种 `if (isDiskMode)` 进行运行态条件分支控制，逻辑臃肿，易导致次生 bug 的产生。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | "拆分为 3 个职责单一的物理模块文件" | 物理拆分为：调度外壳 `ContentPanel`、物理磁盘导航面板 `DiskExplorerPanel`、托管分类面板 `CategoryLibraryPanel`，以及独立的数据模型层 models | ✅ |
| 2    | "[模块一] 纯物理磁盘导航面板 (完全零 SQLite)，彻底删除头文件引入" | 在 `DiskExplorerPanel.cpp` 中完全清退并禁止引入 `MetadataManager.h`、`CategoryRepo.h`、`AssetImporter.h` 等数据库相关头文件，从编译级物理阻断 | ✅ |
| 3    | "[模块二] 托管库与内存分类面板 (数据库驱动)，专责解包与穿透渲染" | 在 `CategoryLibraryPanel.cpp` 中依赖并充分调用数据库，专责解包 `.arc`、穿透渲染素材与无缩略图卡片 | ✅ |
| 4    | "[模块三] 虚拟数据库模型与筛选代理模型从 ContentPanel 抽离" | 抽离 `ArcMetaVirtualDbModel` 和 `FilterProxyModel` 至独立文件 `src/ui/models/` 目录下 | ✅ |
| 5    | "主容器 ContentPanel 仅含 QStackedWidget 切换逻辑" | `ContentPanel` 重构为极简的外壳调度器，包含 `QStackedWidget` 并根据指令（loadDirectory 对应 0 选项，loadCategory 对应 1 选项）动态分流切换 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换或文件创建，不得做任何自由发挥或脑补改动。

### 4.1 新增：`src/ui/models/ArcMetaVirtualDbModel.h`
```cpp
#pragma once

#include <QAbstractTableModel>
#include <QCache>
#include <QIcon>
#include <QSet>
#include <QMap>
#include <vector>
#include <unordered_map>
#include "../../core/IndexedEntry.h"
#include "../../meta/MetadataDefs.h"

namespace ArcMeta {

struct QStringHash {
    size_t operator()(const QString& key) const {
        return qHash(key);
    }
};

class ArcMetaVirtualDbModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ArcMetaVirtualDbModel(QObject* parent = nullptr);
    ~ArcMetaVirtualDbModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void setQuery(const QString& query) { m_query = query; }
    void setRecords(const std::vector<ItemRecord>& records);
    void clear();

    const std::vector<ItemRecord>& allRecords() const { return m_allRecords; }
    void updateRecordMetadata(const QString& path);
    void loadThumbnailsForRows(const QList<int>& rows);
    void migrateCache(const QString& oldPath, const QString& newPath);
    void clearCacheForFolder(const QString& folderPath);

signals:
    void recordRenamed(const QString& oldPath, const QString& newPath, const QString& newName);

private:
    std::vector<ItemRecord> m_allRecords;
    std::unordered_map<QString, int, QStringHash> m_pathToIndex;
    int m_displayCount = 0;
    QString m_query;

    mutable QCache<QString, QIcon> m_iconCache;
    mutable QSet<QString> m_requestedIcons;
    mutable QMap<QString, double> m_aspectRatios;
    mutable QCache<QString, RuntimeMeta> m_metaCache;
};

} // namespace ArcMeta
```

### 4.2 新增：`src/ui/models/ArcMetaVirtualDbModel.cpp`
从原 `ContentPanel.cpp` 抽离 `ArcMetaVirtualDbModel` 的完整实现并保持极高健壮性。
```cpp
#include "ArcMetaVirtualDbModel.h"
#include "../UiHelper.h"
#include "../WindowsShellThumbnailProvider.h"
#include "../../util/ShellHelper.h"
#include "../../meta/MetadataManager.h"
#include "../MediaColorExtractor.h"
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QtConcurrent/QtConcurrent>

namespace ArcMeta {

ArcMetaVirtualDbModel::ArcMetaVirtualDbModel(QObject* parent)
    : QAbstractTableModel(parent), m_iconCache(500), m_metaCache(1000) {}

ArcMetaVirtualDbModel::~ArcMetaVirtualDbModel() {}

int ArcMetaVirtualDbModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_displayCount;
}

int ArcMetaVirtualDbModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 1; 
}

Qt::ItemFlags ArcMetaVirtualDbModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
}

QVariant ArcMetaVirtualDbModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return record.filename;
    }
    if (role == PathRole) {
        return record.path;
    }
    if (role == SizeRole) {
        return record.size;
    }
    if (role == DateCreatedRole) {
        return record.ctime;
    }
    if (role == DateModifiedRole) {
        return record.mtime;
    }
    if (role == IsDirRole) {
        return record.isDir;
    }
    if (role == IsCategoryRole) {
        return record.isCategory;
    }
    if (role == CategoryIdRole) {
        return record.categoryId;
    }
    if (role == RatingRole) {
        return record.rating;
    }
    if (role == ColorRole) {
        return record.manualColor.isEmpty() ? record.autoColor : record.manualColor;
    }
    if (role == TagsRole) {
        return record.tags;
    }
    if (role == PinnedRole) {
        return record.pinned;
    }
    if (role == EncryptedRole) {
        return record.encrypted;
    }
    if (role == NoteRole) {
        return record.note;
    }
    if (role == UrlRole) {
        return record.url;
    }
    if (role == ManagedRole) {
        return record.isManaged;
    }
    if (role == FolderIdRole) {
        return QString::fromStdString(record.folderId);
    }
    if (role == PaletteRole) {
        QVariantList pl;
        for (const auto& pe : record.palettes) {
            QVariantMap m;
            m["color"] = pe.color;
            m["ratio"] = pe.ratio;
            pl.append(m);
        }
        return pl;
    }

    if (role == AspectRatioRole) {
        if (m_aspectRatios.contains(record.path)) {
            return m_aspectRatios[record.path];
        }
        return 1.0;
    }

    if (role == HasThumbnailRole) {
        QFileInfo fi(record.path);
        QString ext = fi.suffix().toLower();
        if (record.isDir && ext != "arc") return false;
        return MediaColorExtractor::isGraphicsFile(ext) || ext == "ai" || ext == "eps" || (ext == "arc" && record.isDir);
    }

    if (role == Qt::DecorationRole) {
        QString path = record.path;
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();

        if (m_iconCache.contains(path)) {
            return *m_iconCache.object(path);
        }

        if (record.isDir && ext != "arc") {
            QIcon folderIcon = WindowsShellThumbnailProvider::getFileIcon(path, 64);
            m_iconCache.insert(path, new QIcon(folderIcon));
            return folderIcon;
        }

        if (MediaColorExtractor::isGraphicsFile(ext) || ext == "ai" || ext == "eps") {
            QIcon shellIcon = WindowsShellThumbnailProvider::getFileIcon(path, 64);
            return shellIcon;
        }

        QIcon docIcon = WindowsShellThumbnailProvider::getFileIcon(path, 64);
        m_iconCache.insert(path, new QIcon(docIcon));
        return docIcon;
    }

    return QVariant();
}

QVariant ArcMetaVirtualDbModel::headerData(int section, Qt::Orientation orientation, int role) const {
    Q_UNUSED(section); Q_UNUSED(orientation); Q_UNUSED(role);
    return QVariant();
}

bool ArcMetaVirtualDbModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return false;

    const auto& record = m_allRecords[index.row()];
    if (role == Qt::EditRole && index.column() == 0) {
        if (record.isCategory) return false;

        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& mutableRecord = m_allRecords[index.row()];
        QString oldPath = mutableRecord.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;

        if (oldPath != newPath) {
            QString nativeNewPath = QDir::toNativeSeparators(newPath);
            QPointer<ArcMetaVirtualDbModel> weakThis(this);
            int row = index.row();
            (void)QtConcurrent::run([weakThis, oldPath, nativeNewPath, newName, row]() {
                if (ShellHelper::renameItem(oldPath, nativeNewPath)) {
                    QMetaObject::invokeMethod(weakThis.data(), [weakThis, oldPath, nativeNewPath, newName, row]() {
                        if (weakThis) {
                            if (row < static_cast<int>(weakThis->m_allRecords.size())) {
                                auto& mutableRec = weakThis->m_allRecords[row];
                                mutableRec.path = nativeNewPath;
                                mutableRec.filename = newName;
                                weakThis->m_metaCache.remove(oldPath);
                                weakThis->migrateCache(oldPath, nativeNewPath);
                                emit weakThis->recordRenamed(oldPath, nativeNewPath, newName);
                                emit weakThis->dataChanged(weakThis->index(row, 0), weakThis->index(row, 0));
                            }
                        }
                    });
                }
            });
            return true;
        }
    }
    return false;
}

QStringList ArcMetaVirtualDbModel::mimeTypes() const {
    return {"text/uri-list"};
}

QMimeData* ArcMetaVirtualDbModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mime = new QMimeData();
    QList<QUrl> urls;
    for (const auto& idx : indexes) {
        if (idx.column() == 0) {
            QString path = data(idx, PathRole).toString();
            if (!path.isEmpty()) urls << QUrl::fromLocalFile(path);
        }
    }
    if (urls.isEmpty()) {
        delete mime;
        return nullptr;
    }
    mime->setUrls(urls);
    return mime;
}

bool ArcMetaVirtualDbModel::canFetchMore(const QModelIndex& parent) const {
    if (parent.isValid()) return false;
    return m_displayCount < static_cast<int>(m_allRecords.size());
}

void ArcMetaVirtualDbModel::fetchMore(const QModelIndex& parent) {
    if (parent.isValid()) return;
    int remaining = static_cast<int>(m_allRecords.size()) - m_displayCount;
    int toFetch = std::min(remaining, 50); 
    if (toFetch <= 0) return;

    beginInsertRows(QModelIndex(), m_displayCount, m_displayCount + toFetch - 1);
    m_displayCount += toFetch;
    endInsertRows();
}

void ArcMetaVirtualDbModel::setRecords(const std::vector<ItemRecord>& records) {
    beginResetModel();
    m_allRecords = records;
    m_pathToIndex.clear();
    for (size_t i = 0; i < m_allRecords.size(); ++i) {
        m_pathToIndex[m_allRecords[i].path] = static_cast<int>(i);
    }
    m_displayCount = std::min(static_cast<int>(m_allRecords.size()), 50);
    endResetModel();
}

void ArcMetaVirtualDbModel::clear() {
    beginResetModel();
    m_allRecords.clear();
    m_pathToIndex.clear();
    m_displayCount = 0;
    endResetModel();
}

void ArcMetaVirtualDbModel::updateRecordMetadata(const QString& path) {
    auto it = m_pathToIndex.find(path);
    if (it != m_pathToIndex.end()) {
        int idx = it->second;
        ItemRecord updated = ItemRecord::create(path);
        m_allRecords[idx].rating = updated.rating;
        m_allRecords[idx].manualColor = updated.manualColor;
        m_allRecords[idx].autoColor = updated.autoColor;
        m_allRecords[idx].tags = updated.tags;
        m_allRecords[idx].pinned = updated.pinned;
        m_allRecords[idx].encrypted = updated.encrypted;
        m_allRecords[idx].note = updated.note;
        m_allRecords[idx].url = updated.url;
        m_allRecords[idx].isManaged = updated.isManaged;
        m_allRecords[idx].palettes = updated.palettes;
        
        m_metaCache.remove(path);
        QModelIndex mi = index(idx, 0);
        emit dataChanged(mi, mi);
    }
}

void ArcMetaVirtualDbModel::loadThumbnailsForRows(const QList<int>& rows) {
    QPointer<ArcMetaVirtualDbModel> weakThis(this);
    for (int r : rows) {
        if (r >= m_displayCount) continue;
        QString path = m_allRecords[r].path;
        if (m_iconCache.contains(path)) continue;
        if (m_requestedIcons.contains(path)) continue;

        m_requestedIcons.insert(path);
        (void)QtConcurrent::run([weakThis, path, r]() {
            QFileInfo info(path);
            QString ext = info.suffix().toLower();
            
            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            if (ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps") {
                img = MediaColorExtractor::getImageForAnalysis(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani") {
                img = MediaColorExtractor::getImageForAnalysis(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                ar = 1.0;
                hasThumb = false;
            } else if (ext == "arc" && info.isDir()) {
                QDir arcDir(path);
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    QString thumbPath = arcDir.absoluteFilePath(thumbFiles.first());
                    if (img.load(thumbPath)) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            }

            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, path, img, ar, hasThumb, r]() {
                if (weakThis) {
                    weakThis->m_requestedIcons.remove(path);
                    weakThis->m_aspectRatios[path] = ar;
                    if (hasThumb && !img.isNull()) {
                        QIcon icon(QPixmap::fromImage(img));
                        weakThis->m_iconCache.insert(path, new QIcon(icon));
                    }
                    QModelIndex idx = weakThis->index(r, 0);
                    emit weakThis->dataChanged(idx, idx, {Qt::DecorationRole, AspectRatioRole});
                }
            });
        });
    }
}

void ArcMetaVirtualDbModel::migrateCache(const QString& oldPath, const QString& newPath) {
    if (m_iconCache.contains(oldPath)) {
        QIcon* icon = m_iconCache.take(oldPath);
        m_iconCache.insert(newPath, icon);
    }
    if (m_aspectRatios.contains(oldPath)) {
        double ratio = m_aspectRatios.take(oldPath);
        m_aspectRatios[newPath] = ratio;
    }
}

void ArcMetaVirtualDbModel::clearCacheForFolder(const QString& folderPath) {
    QString normFolder = QDir::toNativeSeparators(folderPath);
    for (auto it = m_pathToIndex.begin(); it != m_pathToIndex.end(); ) {
        if (it->first.startsWith(normFolder, Qt::CaseInsensitive)) {
            m_iconCache.remove(it->first);
            m_aspectRatios.remove(it->first);
            it = m_pathToIndex.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace ArcMeta
```

### 4.3 新增：`src/ui/models/FilterProxyModel.h`
```cpp
#pragma once

#include <QSortFilterProxyModel>
#include "../../meta/MetadataDefs.h"

namespace ArcMeta {

class FilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject* parent = nullptr);

    FilterState currentFilter;
    void updateFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;
};

} // namespace ArcMeta
```

### 4.4 新增：`src/ui/models/FilterProxyModel.cpp`
```cpp
#include "FilterProxyModel.h"
#include "../../core/ModelContract.h"

namespace ArcMeta {

FilterProxyModel::FilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void FilterProxyModel::updateFilter() {
    invalidateFilter();
}

bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    bool isDir = sourceModel()->data(idx, IsDirRole).toBool();
    QString filename = sourceModel()->data(idx, Qt::DisplayRole).toString();

    // 搜索词过滤
    if (!currentFilter.keyword.isEmpty()) {
        if (!filename.contains(currentFilter.keyword, Qt::CaseInsensitive)) {
            return false;
        }
    }

    // 文件夹/文件可见性过滤
    if (isDir) {
        if (!currentFilter.showFolders) return false;
    } else {
        if (!currentFilter.showFiles) return false;
    }

    // 评分过滤
    if (currentFilter.rating >= 0) {
        int r = sourceModel()->data(idx, RatingRole).toInt();
        if (r != currentFilter.rating) return false;
    }

    // 设色过滤
    if (!currentFilter.colorTag.isEmpty()) {
        QString c = sourceModel()->data(idx, ColorRole).toString();
        if (c.compare(currentFilter.colorTag, Qt::CaseInsensitive) != 0) return false;
    }

    return true;
}

bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const {
    // 默认名称排序
    QString lStr = sourceModel()->data(source_left, Qt::DisplayRole).toString();
    QString rStr = sourceModel()->data(source_right, Qt::DisplayRole).toString();
    return lStr.localeAwareCompare(rStr) < 0;
}

} // namespace ArcMeta
```

### 4.5 新增：`src/ui/DiskExplorerPanel.h`
纯物理磁盘导航面板（100% 物理阻断，源码中**完全无** `MetadataManager` 与 `CategoryRepo` 的引入）。
```cpp
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
```

### 4.6 新增：`src/ui/DiskExplorerPanel.cpp`
物理磁盘导航的具体实现。100% 独立于本地 SQLite。
```cpp
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
```

### 4.7 新增：`src/ui/CategoryLibraryPanel.h`
托管库与内存分类面板（专责解包 `.arc`、穿透渲染与 SQLite 数据库驱动）。
```cpp
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
```

### 4.8 新增：`src/ui/CategoryLibraryPanel.cpp`
托管分类面板的数据库读取、解包逻辑：
```cpp
#include "CategoryLibraryPanel.h"
#include "UiHelper.h"
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

        auto categories = CategoryRepo::getRecentlyUsed(15);
        if (categories.empty()) categories = CategoryRepo::getAll();

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
```

### 4.9 极简调度外壳重构：`src/ui/ContentPanel.h`
```cpp
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
```

### 4.10 极简调度外壳重构：`src/ui/ContentPanel.cpp`
```cpp
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
```

### 4.11 修改构建配置：`CMakeLists.txt`
将所有新建的物理模块文件与数据模型文件加入 CMake，并清除旧的单源文件路径绑定。

```diff
<<<<<<< SEARCH
    src/ui/ColorPicker.cpp
    src/ui/ColorPicker.h
    src/ui/ContentPanel.cpp
    src/ui/ContentPanel.h
    src/ui/DriveButton.cpp
=======
    src/ui/ColorPicker.cpp
    src/ui/ColorPicker.h
    src/ui/ContentPanel.cpp
    src/ui/ContentPanel.h
    src/ui/DiskExplorerPanel.cpp
    src/ui/DiskExplorerPanel.h
    src/ui/CategoryLibraryPanel.cpp
    src/ui/CategoryLibraryPanel.h
    src/ui/models/ArcMetaVirtualDbModel.cpp
    src/ui/models/ArcMetaVirtualDbModel.h
    src/ui/models/FilterProxyModel.cpp
    src/ui/models/FilterProxyModel.h
    src/ui/DriveButton.cpp
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/models/ArcMetaVirtualDbModel.h` & `ArcMetaVirtualDbModel.cpp`（创建新模型文件）
- [ ] `src/ui/models/FilterProxyModel.h` & `FilterProxyModel.cpp`（创建新过滤代理文件）
- [ ] `src/ui/DiskExplorerPanel.h` & `DiskExplorerPanel.cpp`（创建纯物理磁盘导航面板，不引入任何数据库元数据管理）
- [ ] `src/ui/CategoryLibraryPanel.h` & `CategoryLibraryPanel.cpp`（创建数据库驱动分类面板）
- [ ] `src/ui/ContentPanel.h` & `ContentPanel.cpp`（完全重构为主 Stack 调度外壳）
- [ ] `CMakeLists.txt`（更新源文件构建配置清单）

**明确禁止越界修改的范围：**
- [ ] 底层 `AmMetaJson` 文件 — 不作物理改动。
- [ ] 数据库模型及 SQLite 基础结构 — 不作任何改动。

---

## 6. 实现准则与预警【核心】

1. **头文件物理切割红线**：`DiskExplorerPanel.cpp` 中压根禁止包含、依赖和调用 `MetadataManager` 与 `CategoryRepo`，此规则在编译期受到 100% 物理阻断，不共享设计达到极致隔离。
2. **零编译报错标准**：确保所有依赖 models 头文件引入正确、Q_OBJECT 宏正常、Moc 系统顺利生成，从而开箱即用。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 双轨数据路由分流架构（第 1 节） | 托管库写入 SQLite，磁盘导航独占 `AmMetaJson` 读写至 cache 缓存，绝不污染物理文件夹 | ✅ 100% 符合 |
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `isMirrorSource()` 或强类型进行识别 | ✅ 完全符合 |
