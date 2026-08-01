#include "ArcMetaVirtualDbModel.h"
#include "../UiHelper.h"
#include "../../core/ModelContract.h"
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
            m["color"] = pe.first;
            m["ratio"] = pe.second;
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
