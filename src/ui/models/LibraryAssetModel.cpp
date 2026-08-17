#include "LibraryAssetModel.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "ModelContract.h"
#include "../ContentPanel.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFileIconProvider>

using namespace ArcMeta;

#include "../../meta/MetadataManager.h"
#include "../../meta/CategoryRepo.h"
#include "../../meta/CapsuleMediaExtractor.h"
#include "../../meta/MediaExtractorPipeline.h"
#include "../core/UndoManager.h"
#include "../MemoryBatchRenameService.h"
#include "../core/BasicCommands.h"
#include "MediaColorExtractor.h"
#include "../../meta/FileOperationHelper.h"
#include <QtConcurrent>
#include <QSvgRenderer>
#include <QPainter>

LibraryAssetModel::LibraryAssetModel(QObject* parent) : ItemModelBase(parent) {
    m_iconCache.setMaxCost(500);
}

LibraryAssetModel::~LibraryAssetModel() {}

int LibraryAssetModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_allRecords.size());
}

int LibraryAssetModel::columnCount(const QModelIndex&) const {
    return 7;
}

QVariant LibraryAssetModel::headerData(int section, Qt::Orientation orientation, int role) const {
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

    // 处理 F2 / 右键菜单行内重命名提交
    if (role == Qt::EditRole && index.column() == 0) {
        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& record = m_allRecords[index.row()];
        QString oldPath = record.path;
        QFileInfo oldInfo(oldPath);

        if (newName == oldInfo.fileName() || newName == oldInfo.completeBaseName()) return true;

        QString suffix = oldInfo.suffix();
        if (!suffix.isEmpty() && !newName.endsWith("." + suffix, Qt::CaseInsensitive)) {
            newName += "." + suffix;
        }

        // 调用内存胶囊模式物理改名与索引同步
        bool success = false;
        QDir arcDir = oldInfo.absoluteDir(); // 直接定位到 .arc 胶囊目录
        QString newBaseName = QFileInfo(newName).completeBaseName();
        QString newMainPath = arcDir.filePath(newName);

        if (oldPath == newMainPath) {
            success = true;
        } else if (FileOperationHelper::safeRename(oldPath, newMainPath)) {
            success = true;
            // 物理扫描 .arc 胶囊目录，精准强杀并重命名 *_thumbnail.png
            QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
            for (const QString& oldThumbName : thumbFiles) {
                QString oldThumbAbsPath = arcDir.filePath(oldThumbName);
                QString newThumbAbsPath = arcDir.filePath(newBaseName + "_thumbnail.png");
                if (oldThumbAbsPath != newThumbAbsPath) {
                    FileOperationHelper::safeRename(oldThumbAbsPath, newThumbAbsPath);
                }
            }

            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir::toNativeSeparators(newMainPath).toStdWString();

            // 更新内存数据库与索引
            MetadataManager::instance().renameItem(oldW, newW);
            CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
        }

        if (success) {
            QString newPath = QDir(oldInfo.absolutePath()).filePath(newName);
            record.path = newPath;
            record.filename = newName;

            m_pathToIndex.erase(oldPath);
            m_pathToIndex[newPath] = index.row();

            // 物理与虚拟并轨：将单项重命名动作作为 RenameCommand 推送入全局 UndoManager 撤销栈
            // 对应用户原话：“在弹出 UndoToastOverlay 并点击撤销按钮时，能成功撤销吗？”
            UndoManager::instance().pushCommand(std::make_unique<RenameCommand>(oldPath, newPath));

            emit recordRenamed(oldPath, newPath, newName);
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
            return true;
        }
        return false;
    }

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

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
    Qt::ItemFlags f = QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
    if (index.column() == 0) {
        f |= Qt::ItemIsEditable; // 🚨 解封内存模式第 0 列的编辑权限！
    }
    return f;
}

void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    uint64_t myGen = ++m_currentGen;

    std::vector<QString> newQueue;
    std::vector<std::wstring> unbakedWPaths;

    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        bool isArcContainer = rec.isDir && rec.path.endsWith(".arc", Qt::CaseInsensitive);
        bool needLoad = !m_iconCache.contains(path);
        if ((UiHelper::isGraphicsFile(rec.suffix) || isArcContainer) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
            needLoad = true;
        }

        if (m_requestedIcons.contains(path)) {
            needLoad = false;
        }

        if (needLoad) {
            m_requestedIcons.insert(path);
            newQueue.push_back(path);
            unbakedWPaths.push_back(path.toStdWString());
        }
    }

    if (newQueue.empty()) return;

    // 🚀【改造点 1】：将当前视口定位的缺失缩略图素材批量插队至 MediaExtractorPipeline 最前端，强行优先生成！
    if (!unbakedWPaths.empty()) {
        MediaExtractorPipeline::instance().prioritizeBatch(unbakedWPaths);
    }

    // 🚀【改造点 2】：聚合为单个工作线程批量调度 + Generation 机制，消除无界线程风暴
    QPointer<LibraryAssetModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue, myGen]() {
        struct LoadedItem {
            QString path;
            QIcon icon;
            double ar{1.0};
            bool hasThumb{false};
        };
        std::vector<LoadedItem> loadedBatch;
        loadedBatch.reserve(newQueue.size());

        for (const QString& path : newQueue) {
            if (!weakThis || weakThis->m_currentGen.load() != myGen) {
                // 视口已滑动触发新一轮加载，旧视口任务立刻熔断！
                break;
            }

            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);

            if (isInsideArc || ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps" || UiHelper::isGraphicsFile(ext)) {
                img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
                if (img.isNull()) {
                    img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 512);
                }
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive)) && info.isDir()) {
                QString cleanPath = path;
                if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                    cleanPath = cleanPath.left(cleanPath.length() - 1);
                }
                QDir arcDir(cleanPath);
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    img = QImage(cleanPath + "/" + thumbFiles.first());
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            }

            QIcon icon;
            if (!img.isNull()) {
                icon = QIcon(QPixmap::fromImage(img));
            } else {
                QString iconTarget = path;
                if (ext == "arc" && info.isDir()) {
                    QString cleanPath = path;
                    if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                        cleanPath = cleanPath.left(cleanPath.length() - 1);
                    }
                    QDir arcDir(cleanPath);
                    QStringList files = arcDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QString& fn : files) {
                        if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                        if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                        iconTarget = cleanPath + "/" + fn;
                        break;
                    }
                }
                icon = ShellIconManager::getFileIcon(iconTarget, 128);
            }

            loadedBatch.push_back({path, icon, ar, hasThumb});
        }

        if (loadedBatch.empty()) {
            if (weakThis) {
                QMetaObject::invokeMethod(weakThis.data(), [weakThis, newQueue]() {
                    if (weakThis) {
                        for (const auto& p : newQueue) weakThis->m_requestedIcons.remove(p);
                    }
                });
            }
            return;
        }

        QMetaObject::invokeMethod(weakThis.data(), [weakThis, loadedBatch, newQueue, myGen]() {
            if (weakThis) {
                // 清理所有请求标记
                for (const auto& p : newQueue) weakThis->m_requestedIcons.remove(p);

                // 若这批数据到达时已被新代数覆盖，作废UI绘制
                if (weakThis->m_currentGen.load() != myGen) return;

                int minRow = -1;
                int maxRow = -1;

                for (const auto& item : loadedBatch) {
                    weakThis->m_iconCache.insert(item.path, new QIcon(item.icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(item.path)] = item.hasThumb ? item.ar : -1.0;

                    auto it = weakThis->m_pathToIndex.find(item.path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (minRow == -1 || rIdx < minRow) minRow = rIdx;
                        if (maxRow == -1 || rIdx > maxRow) maxRow = rIdx;

                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        }
                    }
                }

                if (!weakThis->isSuspended() && minRow != -1 && maxRow != -1) {
                    emit weakThis->dataChanged(weakThis->index(minRow, 0), weakThis->index(maxRow, 0), {Qt::DecorationRole, HasThumbnailRole});
                }
            }
        });
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

    // 分组标题特异分支 (双轨隔离回收站)
    if (record.isGroupHeader) {
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return record.filename;
        } else if (role == IsGroupHeaderRole) {
            return true;
        } else if (role == GroupNameRole) {
            return record.groupName;
        } else if (role == TypeRole) {
            return "group_header";
        } else if (role == PathRole) {
            return "";
        }
        return QVariant();
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
    } else if (role == IsGroupHeaderRole) {
        return record.isGroupHeader;
    } else if (role == GroupNameRole) {
        return record.groupName;
    } else if (role == IsDiskTrashRole) {
        return record.isDiskTrash;
    } else if (role == DiskTrashIdRole) {
        return record.diskTrashId;
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        double ratio = m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
        return ratio > 0.0 ? ratio : 1.0;
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;

        // 纯内存字符串快速匹配，零 QFileInfo 构造与系统调用
        bool isInsideArcContainer = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);
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
        QString cacheKey = path;
        QIcon* cached = m_iconCache.object(cacheKey);
        if (cached) return *cached;

        QString ext = record.suffix.toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";
        
        // .arc 资产包容器内部文件：判断父目录是否为 .arc 容器，等待异步加载
        bool isInsideArcContainer = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);

        if (isGraphic || isInsideArcContainer || isArcContainer) return QIcon(); 
        QIcon icon = ShellIconManager::getFileIconFast(path, record.isDir, ext);
        if (ShellIconManager::isIconCached(path, record.isDir, ext)) {
            m_iconCache.insert(cacheKey, new QIcon(icon));
        }
        return icon;
    }

    return QVariant();
}

bool LibraryAssetModel::isSuspended() const {
    auto* cp = qobject_cast<ContentPanel*>(parent());
    return cp && cp->isContextMenuActive();
}

void LibraryAssetModel::flushPendingUpdates() {
    if (m_pendingUpdateRows.isEmpty()) return;
    for (int rIdx : m_pendingUpdateRows) {
        emit dataChanged(index(rIdx, 0), index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
    }
    m_pendingUpdateRows.clear();
}
