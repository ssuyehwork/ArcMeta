#include "DiskItemModel.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "ModelContract.h"
#include "../ContentPanel.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include "../meta/QuarkMetaJson.h"
#include "../ui/MediaColorExtractor.h" // 🚨 补全头文件引入
#include "../../util/DiskMediaExtractor.h"
#include "../DiskBatchRenameService.h"
#include "../../meta/FileOperationHelper.h"
#include "../../meta/CapsuleMediaExtractor.h"
#include "../../meta/MetadataManager.h"
#include "../../meta/CategoryRepo.h"

using namespace QuarkMeta;

#include <QtConcurrent>

DiskItemModel::DiskItemModel(QObject* parent) : ItemModelBase(parent) {
    m_iconCache.setMaxCost(500);
}

DiskItemModel::~DiskItemModel() {}

int DiskItemModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_allRecords.size());
}

int DiskItemModel::columnCount(const QModelIndex&) const {
    return 7;
}

QVariant DiskItemModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case 0: return QString("名称");
            case 1: return QString("状态");
            case 2: return QString("评分");
            case 3: return QString("尺寸");
            case 4: return QString("类型");
            case 5: return QString("大小");
            case 6: return QString("修改日期");
            default: break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

void DiskItemModel::setRecords(const std::vector<ItemRecord>& records) {
    beginResetModel();
    m_allRecords = records;
    m_pathToIndex.clear();
    m_requestedPaths.clear(); // 🚨 清空请求锁
    for (int i = 0; i < static_cast<int>(m_allRecords.size()); ++i) {
        m_pathToIndex[m_allRecords[i].path] = i;
    }
    m_iconCache.setMaxCost(qMax(500, static_cast<int>(m_allRecords.size()) + 50));
    m_requestedIcons.clear();
    endResetModel();
}

void DiskItemModel::clear() {
    beginResetModel();
    m_allRecords.clear();
    m_pathToIndex.clear();
    m_requestedPaths.clear(); // 🚨 清空请求锁
    m_query.clear();
    m_requestedIcons.clear();
    m_aspectRatios.clear();
    endResetModel();
}

void DiskItemModel::updateRecordMetadata(const QString& path) {
    QString nPath = QDir::toNativeSeparators(path);
    auto it = m_pathToIndex.find(nPath);
    if (it != m_pathToIndex.end()) {
        int i = it->second;
        if (i >= 0 && i < static_cast<int>(m_allRecords.size())) {
            auto& record = m_allRecords[i];
            QFileInfo fileInfo(nPath);
            QString parentDir = QDir::toNativeSeparators(fileInfo.absolutePath());
            QString fileName = fileInfo.fileName();

            QuarkMetaJson jsonCache(parentDir.toStdWString());
            jsonCache.load();
            const auto& cachedItems = jsonCache.items();
            auto cachedIt = cachedItems.find(fileName.toStdWString());
            if (cachedIt != cachedItems.end()) {
                record.rating = cachedIt->second.rating;
                record.manualColor = QString::fromStdWString(cachedIt->second.color);
                record.pinned = cachedIt->second.pinned;
                record.note = QString::fromStdWString(cachedIt->second.note);
                record.url = QString::fromStdWString(cachedIt->second.url);
                record.tags.clear();
                for (const auto& t : cachedIt->second.tags) {
                    record.tags.append(QString::fromStdWString(t));
                }
                record.width = cachedIt->second.width;
                record.height = cachedIt->second.height;
                record.autoColor = QString::fromStdWString(cachedIt->second.autoColor);
                record.added_at = cachedIt->second.addedAt;

                record.palettes.clear();
                for (const auto& pe : cachedIt->second.palettes) {
                    record.palettes.push_back({pe.color, pe.ratio});
                }
            }
            emit dataChanged(index(i, 0), index(i, columnCount() - 1));
        }
    }
}

bool DiskItemModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return false;

    // 处理 F2 / 右键菜单行内重命名提交
    if (role == Qt::EditRole && index.column() == 0) {
        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& record = m_allRecords[index.row()];
        QString oldPath = record.path;
        QFileInfo oldInfo(oldPath);

        // 如果名字没有改变，直接返回
        if (newName == oldInfo.fileName() || newName == oldInfo.completeBaseName()) return true;

        // 补全后缀
        QString suffix = oldInfo.suffix();
        if (!suffix.isEmpty() && !newName.endsWith("." + suffix, Qt::CaseInsensitive)) {
            newName += "." + suffix;
        }

        // 调用磁盘模式物理改名与索引及缩略图同步
        bool success = false;
        QString destDir = oldInfo.absolutePath();
        QString newPathStr = QDir(destDir).filePath(newName);

        if (oldPath == newPathStr) {
            success = true;
        } else if (FileOperationHelper::safeRename(oldPath, newPathStr)) {
            success = true;

            // 同步对 .QuarkMeta/disk_thumbs/ 中的哈希缩略图进行重命名
            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPath);
            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);

            if (QFile::exists(oldThumbHashPath)) {
                FileOperationHelper::safeRename(oldThumbHashPath, newThumbHashPath);
            }

            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir(destDir).absoluteFilePath(newPathStr).toStdWString();

            MetadataManager::instance().renameItem(oldW, newW);
            CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
        }

        if (success) {
            QString newPath = QDir(oldInfo.absolutePath()).filePath(newName);
            record.path = newPath;
            record.filename = newName;
            
            // 更新路径映射索引
            m_pathToIndex.erase(oldPath);
            m_pathToIndex[newPath] = index.row();
            
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
            return true;
        }
        return false;
    }

    auto& record = m_allRecords[index.row()];
    QString path = record.path;
    QFileInfo fileInfo(path);
    QString parentDir = QDir::toNativeSeparators(fileInfo.absolutePath());
    QString fileName = fileInfo.fileName();

    bool metaUpdated = false;

    QuarkMetaJson jsonCache(parentDir.toStdWString());
    jsonCache.load();
    auto& cachedItems = jsonCache.items();
    
    std::wstring wFileName = fileName.toStdWString();
    if (cachedItems.find(wFileName) == cachedItems.end()) {
        ItemMeta emptyMeta;
        emptyMeta.type = record.isDir ? L"folder" : L"file";
        cachedItems[wFileName] = emptyMeta;
    }
    auto& fileMeta = cachedItems[wFileName];

    if (role == RatingRole) {
        int newRating = value.toInt();
        if (record.rating != newRating) {
            record.rating = newRating;
            fileMeta.rating = newRating;
            metaUpdated = true;
        }
    } else if (role == ColorRole) {
        QString newColor = value.toString();
        if (record.manualColor != newColor) {
            record.manualColor = newColor;
            fileMeta.color = newColor.toStdWString();
            metaUpdated = true;
        }
    } else if (role == IsLockedRole || role == PinnedRole) {
        bool pinned = value.toBool();
        if (record.pinned != pinned) {
            record.pinned = pinned;
            fileMeta.pinned = pinned;
            metaUpdated = true;
        }
    }

    if (metaUpdated) {
        jsonCache.save();
        emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
        return true;
    }
    return false;
}

void DiskItemModel::migrateCache(const QString& oldPath, const QString& newPath) {
    QString nativeOld = QDir::toNativeSeparators(oldPath);
    QString nativeNew = QDir::toNativeSeparators(newPath);
    QIcon* oldIconPtr = m_iconCache.take(oldPath);
    if (oldIconPtr) {
        m_iconCache.insert(nativeNew, oldIconPtr);
    }
    if (m_aspectRatios.contains(nativeOld)) {
        double ratio = m_aspectRatios.take(nativeOld);
        m_aspectRatios[nativeNew] = ratio;
    }
}

void DiskItemModel::clearCacheForFolder(const QString& folderPath) {
    QString nativeFolder = QDir::toNativeSeparators(folderPath);
    QString prefix = nativeFolder;
    if (!prefix.endsWith(QDir::separator())) prefix += QDir::separator();

    for (auto it = m_aspectRatios.begin(); it != m_aspectRatios.end(); ) {
        if (it.key() == nativeFolder || it.key().startsWith(prefix)) {
            it = m_aspectRatios.erase(it);
        } else {
            ++it;
        }
    }
}

void DiskItemModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isDir) continue;
        
        QString path = rec.path;
        if (!UiHelper::isGraphicsFile(rec.suffix)) continue;

        if (m_iconCache.contains(path) || m_requestedPaths.contains(path)) continue;

        m_requestedPaths.insert(path);
        newQueue.push_back({path, path});
    }

    if (newQueue.empty()) return;

    QPointer<DiskItemModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;

            QImage img = DiskMediaExtractor::getDiskThumbnail(path, 512);

            double ar = 1.0;
            bool hasThumb = false;
            if (!img.isNull()) {
                ar = (double)img.width() / img.height();
                hasThumb = true;
            }

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar, hasThumb]() {
                if (weakThis) {
                    QIcon icon = img.isNull() ? ShellIconManager::getFileIcon(path, 128) : QIcon(QPixmap::fromImage(img));
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedPaths.remove(path);

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                        }
                    }
                }
            });
        }
    });
}

Qt::ItemFlags DiskItemModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return QAbstractTableModel::flags(index);
    Qt::ItemFlags f = QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
    if (index.column() == 0) {
        f |= Qt::ItemIsEditable; // 🚨 解封第 0 列（文件名列）的行内编辑权限！
    }
    return f;
}

QVariant DiskItemModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: {
                int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
                if (lastSlash == -1) return path;
                QString name = path.mid(lastSlash + 1);
                if (name.isEmpty() && path.length() >= 2 && path[1] == ':') return path; // 盘符根目录安全保护
                return name;
            }
            case 3: {
                if (record.isDir) return "-";
                if (record.width > 0 && record.height > 0) {
                    return QString("%1 x %2").arg(record.width).arg(record.height);
                }
                return "-";
            }
            case 4: {
                if (record.isDir) return "文件夹";
                int lastDot = path.lastIndexOf('.');
                return (lastDot != -1) ? path.mid(lastDot + 1).toUpper() : "";
            }
            case 5: {
                if (record.isDir) return "-";
                if (record.size < 1024) return QString::number(record.size) + " B";
                if (record.size < 1024 * 1024) return QString::number(record.size / 1024.0, 'f', 1) + " KB";
                return QString::number(record.size / (1024.0 * 1024.0), 'f', 1) + " MB";
            }
            case 6: {
                return QDateTime::fromMSecsSinceEpoch(record.mtime).toString("dd-MM-yyyy HH:mm");
            }
        }
    } else if (role == PathRole) {
        return path;
    } else if (role == TypeRole) {
        return record.isDir ? "folder" : "file";
    } else if (role == RatingRole) {
        return record.rating;
    } else if (role == ColorRole) {
        return record.manualColor;
    } else if (role == IsLockedRole || role == PinnedRole) {
        return record.pinned;
    } else if (role == EncryptedRole) {
        return record.encrypted;
    } else if (role == TagsRole) {
        return record.tags;
    } else if (role == ManagedRole) {
        return record.isManaged;
    } else if (role == RegistrationProgressRole) {
        return record.registrationProgress;
    } else if (role == CategoryIdRole) {
        return 0; 
    } else if (role == IsEmptyRole) {
        return record.isDir && record.isEmpty; // 磁盘模式专享：物理判断是否为空文件夹
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        double ratio = m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
        return ratio > 0.0 ? ratio : 1.0;
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QString cacheKey = path;
        QIcon* cached = m_iconCache.object(cacheKey);
        if (cached) return *cached;

        QString ext = record.suffix.toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";
        
        if (isGraphic) return QIcon(); // 图形文件等待异步加载时返回空图标
        QIcon icon = ShellIconManager::getFileIconFast(path, record.isDir, ext);
        if (ShellIconManager::isIconCached(path, record.isDir, ext)) {
            m_iconCache.insert(cacheKey, new QIcon(icon));
        }
        return icon;
    }

    return QVariant();
}

bool DiskItemModel::isSuspended() const {
    auto* cp = qobject_cast<ContentPanel*>(parent());
    return cp && cp->isContextMenuActive();
}

void DiskItemModel::flushPendingUpdates() {
    if (m_pendingUpdateRows.isEmpty()) return;
    for (int rIdx : m_pendingUpdateRows) {
        emit dataChanged(index(rIdx, 0), index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
    }
    m_pendingUpdateRows.clear();
}