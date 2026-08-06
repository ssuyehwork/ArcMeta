#include "LibraryAssetModel.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "ModelContract.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFileIconProvider>

using namespace ArcMeta;

#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
#include "MediaColorExtractor.h"
#include <QtConcurrent>
#include <QSvgRenderer>
#include <QPainter>

LibraryAssetModel::LibraryAssetModel(QObject* parent) : ItemModelBase(parent) {
    m_iconCache.setMaxCost(500);

    // 🚨 新增：缩略图重试定时器，800ms 一轮，覆盖大多数"文件还没生成完"的时间窗口
    m_thumbRetryTimer = new QTimer(this);
    m_thumbRetryTimer->setInterval(800);
    connect(m_thumbRetryTimer, &QTimer::timeout, this, [this]() {
        if (m_thumbRetryCount.isEmpty()) {
            m_thumbRetryTimer->stop();
            return;
        }
        QList<int> rows;
        for (auto it = m_thumbRetryCount.begin(); it != m_thumbRetryCount.end(); ++it) {
            auto pit = m_pathToIndex.find(it.key());
            if (pit != m_pathToIndex.end()) rows.append(pit->second);
        }
        if (!rows.isEmpty()) {
            loadThumbnailsForRows(rows); // 复用现有请求通道，走正常的去重/加载流程
        }
    });
}

LibraryAssetModel::~LibraryAssetModel() {}

int LibraryAssetModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_allRecords.size());
}

int LibraryAssetModel::columnCount(const QModelIndex&) const {
    return 7;
}

void LibraryAssetModel::setRecords(const std::vector<ItemRecord>& records) {
    beginResetModel();
    m_allRecords = records;
    m_pathToIndex.clear();
    for (int i = 0; i < static_cast<int>(m_allRecords.size()); ++i) {
        m_pathToIndex[m_allRecords[i].path] = i;
    }
    m_iconCache.setMaxCost(qMax(500, static_cast<int>(m_allRecords.size()) + 50));
    m_requestedIcons.clear();
    m_metaCache.clear();
    m_thumbRetryCount.clear();      // 新增
    if (m_thumbRetryTimer) m_thumbRetryTimer->stop(); // 新增
    endResetModel();
}

void LibraryAssetModel::clear() {
    beginResetModel();
    m_allRecords.clear();
    m_pathToIndex.clear();
    m_query.clear();
    m_requestedIcons.clear();
    m_aspectRatios.clear();
    m_metaCache.clear();
    m_thumbRetryCount.clear();      // 新增
    if (m_thumbRetryTimer) m_thumbRetryTimer->stop(); // 新增
    endResetModel();
}

void LibraryAssetModel::updateRecordMetadata(const QString& path) {
    QString nPath = QDir::toNativeSeparators(path);
    auto it = m_pathToIndex.find(nPath);
    if (it != m_pathToIndex.end()) {
        int i = it->second;
        if (i >= 0 && i < static_cast<int>(m_allRecords.size())) {
            auto meta = MetadataManager::instance().getMeta(nPath.toStdWString());
            ItemRecord::fromMetadata(m_allRecords[i], meta);
            m_metaCache.remove(nPath);
            emit dataChanged(index(i, 0), index(i, columnCount() - 1));
        }
    }
}

void LibraryAssetModel::migrateCache(const QString& oldPath, const QString& newPath) {
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

void LibraryAssetModel::clearCacheForFolder(const QString& folderPath) {
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

bool LibraryAssetModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return false;

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

    if (role == Qt::EditRole && index.column() == 0) {
        return false; // 内存模式重命名由 ContentPanel 统一处理
    }

    bool metaUpdated = false;
    if (role == RatingRole) {
        int oldRating = index.data(RatingRole).toInt();
        int newRating = value.toInt();
        if (oldRating != newRating) {
            if (record.isCategory) {
                auto& mutableRec = m_allRecords[index.row()];
                mutableRec.rating = newRating;
                metaUpdated = true;
            } else {
                MetadataManager::instance().setRating(path.toStdWString(), newRating);
                UndoManager::instance().pushCommand(std::make_unique<MetadataCommand>(path, MetadataCommand::Rating, oldRating, newRating));
                metaUpdated = true;
            }
        }
    } else if (role == ColorRole) {
        QString oldColor = index.data(ColorRole).toString();
        QString newColor = value.toString();
        if (oldColor != newColor) {
            auto& mutableRec = m_allRecords[index.row()];
            if (record.isCategory) {
                auto all = CategoryRepo::getAll();
                for (auto& c : all) {
                    if (c.id == record.categoryId) {
                        c.color = newColor.toUpper().toStdWString();
                        CategoryRepo::update(c);
                        if (!c.physicalPath.empty()) {
                            MetadataManager::instance().setColor(c.physicalPath, c.color, false);
                        }
                        break;
                    }
                }
                mutableRec.categoryColor = newColor;
                metaUpdated = true;
            } else {
                MetadataManager::instance().setColor(path.toStdWString(), newColor.toStdWString(), false);
                if (record.isDir) {
                    std::wstring normPath = MetadataManager::normalizePath(path.toStdWString());
                    CategoryRepo::updateCategoryColorByPath(normPath, newColor.toUpper().toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MetadataCommand>(path, MetadataCommand::Color, oldColor, newColor));
                metaUpdated = true;
            }
        }
    } else if (role == IsLockedRole || role == PinnedRole) {
        bool pinned = value.toBool();
        if (record.isCategory) {
            auto all = CategoryRepo::getAll();
            for (auto& c : all) {
                if (c.id == record.categoryId) {
                    c.pinned = pinned;
                    CategoryRepo::update(c);
                    auto& mutableRec = m_allRecords[index.row()];
                    mutableRec.pinned = pinned;
                    metaUpdated = true;
                    break;
                }
            }
        } else {
            MetadataManager::instance().setPinned(path.toStdWString(), pinned);
            metaUpdated = true;
        }
    }

    if (metaUpdated) {
        if (!record.isCategory) {
            m_metaCache.remove(path);
            updateRecordMetadata(path);
        } else {
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CategoryOnly);
        }
        return true;
    }
    return false;
}

Qt::ItemFlags LibraryAssetModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return QAbstractTableModel::flags(index);
    return QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
}

void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        if (!m_iconCache.contains(path) && !m_requestedIcons.contains(path)) {
            m_requestedIcons.insert(path);
            newQueue.push_back({path, path});
        }
    }

    if (newQueue.empty()) return;

    QPointer<LibraryAssetModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;

            // 纯只读读取已有的 _thumbnail.png
            QImage img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 128);
            double ar = !img.isNull() ? (double)img.width() / img.height() : -1.0;

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar]() {
                if (weakThis) {
                    weakThis->m_requestedIcons.remove(path); // 释放请求防重锁

                    if (!img.isNull()) {
                        weakThis->m_iconCache.insert(path, new QIcon(QPixmap::fromImage(img)));
                        weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = ar;
                        weakThis->m_thumbRetryCount.remove(path); // 成功了，清掉重试记录
                    } else {
                        weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = -1.0;

                        // 🚨 核心修复：不再"查一次没有就永久放弃"。
                        // 缩略图很可能是后台流水线还没来得及生成，限次重试（最多5轮，每轮800ms）
                        int& cnt = weakThis->m_thumbRetryCount[path];
                        if (cnt < 5) {
                            cnt++;
                            if (!weakThis->m_thumbRetryTimer->isActive()) {
                                weakThis->m_thumbRetryTimer->start();
                            }
                        } else {
                            weakThis->m_thumbRetryCount.remove(path); // 重试次数用尽才真正放弃（大概率是真无缩略图，如损坏文件）
                        }
                    }

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0),
                                                 {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                    }
                }
            });
        }
    });
}

QVariant LibraryAssetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;
    bool isArcEnd = path.endsWith(".arc", Qt::CaseInsensitive) || path.endsWith(".arc/", Qt::CaseInsensitive) || path.endsWith(".arc\\", Qt::CaseInsensitive);
    if (isArcEnd && (path.endsWith("/") || path.endsWith("\\"))) {
        path = path.left(path.length() - 1);
    }

    // 分类节点及子分类专用大分支（对应用户原话：“LibraryAssetModel 只处理内存数据库模式条目（包含 isCategory 分支）”）
    if (record.isCategory) {
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
                case 0: return record.categoryName;
                case 4: return "子分类";
                default: return "";
            }
        } else if (role == CategoryIdRole) {
            return record.categoryId;
        } else if (role == ColorRole) {
            return record.categoryColor;
        } else if (role == RatingRole) {
            return record.rating;
        } else if (role == TypeRole) {
            return "category";
        } else if (role == PathRole) {
            return record.path;
        } else if (role == IsLockedRole || role == PinnedRole) {
            return record.pinned;
        } else if (role == Qt::DecorationRole && index.column() == 0) {
            static QIcon catIcon = QFileIconProvider().icon(QFileIconProvider::Folder);
            return catIcon;
        }
        return QVariant();
    }

    // 内存托管库内已解包条目
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: {
                // 优先使用 ItemRecord 中解包好的真实素材文件名（对应用户原话：“内存模式下彻底解包 .arc 容器，显示真实素材文件名”）
                if (!record.filename.isEmpty()) return record.filename;
                int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
                if (lastSlash == -1) return path;
                QString name = path.mid(lastSlash + 1);
                if (name.isEmpty() && path.length() >= 2 && path[1] == ':') return path;
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
        return false; // 内存模式不使用物理空文件夹状态
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        double ratio = m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
        return ratio > 0.0 ? ratio : 1.0;
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;

        QFileInfo pInfo(path);
        bool isInsideArcContainer = pInfo.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);
        if (isInsideArcContainer || isArcContainer) {
            QString nativePath = QDir::toNativeSeparators(path);
            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
        }

        if (record.suffix.toLower() == "ai") {
            QString nativePath = QDir::toNativeSeparators(path);
            if (m_aspectRatios.contains(nativePath)) {
                return m_aspectRatios.value(nativePath) > 0.0;
            }
            return false;
        }
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QIcon* cached = m_iconCache.object(path);
        if (cached) return *cached;

        // 🚨 核心优化：彻底移除 QFileInfo 和 info.dir() 磁盘调用，改用 pre-baked 的 record 字段
        QString ext = record.suffix.toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);

        if (isGraphic || isArcContainer) return QIcon();

        // 对非图片文件使用系统默认文件夹/文件图标兜底，绝不在 paint 路径上同步调 Shell API 阻断 UI
        static QIcon defaultFileIcon = QFileIconProvider().icon(QFileIconProvider::File);
        static QIcon defaultFolderIcon = QFileIconProvider().icon(QFileIconProvider::Folder);
        return record.isDir ? defaultFolderIcon : defaultFileIcon;
    }

    return QVariant();
}
