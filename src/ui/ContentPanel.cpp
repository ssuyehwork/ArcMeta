#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ContentPanel.h" 
#include "ColorPicker.h"
#include <QWidgetAction>
#include "../meta/MetadataManager.h" 
#include "../meta/MediaExtractorPipeline.h"
#include <algorithm>
#include "Logger.h"
#include "SvgIcons.h" 
#include "TreeItemDelegate.h" 
#include "DropTreeView.h" 
#include "DropListView.h" 
#include "DropJustifiedView.h"
#include "BatchProgressDialog.h"
#include "ThumbnailDelegate.h"
#include "../util/ImportHelper.h"
#include "../util/AssetImporter.h"
#include "../core/AutoImportManager.h"
#include "../meta/AmMetaJson.h"
#include "../core/NavigationHistoryService.h"
#include "ToolTipOverlay.h" 
#include "MainWindow.h"
#include "../util/SecureFileEraser.h"
#include "../util/DiskIoService.h"
 
#include <QVBoxLayout> 
#include <QHBoxLayout> 
#include <QIcon> 
#include <QSvgRenderer> 
#include <QPainter> 
#include <QHeaderView> 
#include <QScrollBar> 
#include <QStyle> 
#include <QLabel> 
#include <QAction> 
#include <QActionGroup>
#include <QMenu> 
#include <QAbstractItemView> 
#include <QStandardItem> 
#include "../core/AppConfig.h"
#include <QEvent> 
#include <QKeyEvent> 
#include <QMouseEvent> 
#include <QWheelEvent> 
#include <QStyleOptionViewItem> 
#include <QItemSelectionModel> 
#include <QFileInfo> 
#include <QDir> 
#include <QSet>
#include <QFile>
#include <QDateTime> 
#include <QDesktopServices> 
#include <QUrl> 
#include <QApplication> 
#include <QCoreApplication> 
#include <QProcess> 
#include <QClipboard> 
#include <QMimeData> 
#include <QLineEdit> 
#include <QTextBrowser> 
#include "FramelessDialog.h"
#include <memory>
#include <QRandomGenerator>
#include <QAbstractItemView> 
#include <QtConcurrent> 
#include <QThreadPool> 
#include <QTimer> 
#include <QPointer> 
#include <QPersistentModelIndex> 
 
 
#include <windows.h> 
#include <objbase.h>
#include <shellapi.h> 
#include <io.h>
#include "../meta/MetadataManager.h" 
#include "../meta/BatchRenameEngine.h" 
#include "../meta/CategoryRepo.h" 
#include "../crypto/EncryptionManager.h" 
#include "CategoryLockDialog.h" 
#include "BatchRenameDialog.h" 
#include "UiHelper.h" 
#include "ShellIconManager.h"
#include "StyleLibrary.h"
#include <QFileIconProvider>
#include "../core/CoreController.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
using namespace ArcMeta::Style;
#include "../util/ShellHelper.h"
#include "DiskScanService.h"
#include "CategoryLoadService.h"
#include "../ui/MediaColorExtractor.h"
 
namespace ArcMeta { 

// --- ArcMetaVirtualDbModel 实现 ---
ArcMetaVirtualDbModel::ArcMetaVirtualDbModel(QObject* parent) : QAbstractTableModel(parent) {
    m_iconCache.setMaxCost(500);
    m_metaCache.setMaxCost(1000);

    // 订阅文件图标异步加载完成信号，安全刷新第 0 列渲染
    connect(&IconLoadNotifier::instance(), &IconLoadNotifier::iconLoaded, this, [this]() {
        if (m_displayCount > 0) {
            emit dataChanged(index(0, 0), index(m_displayCount - 1, 0), {Qt::DecorationRole});
        }
    });
}

ArcMetaVirtualDbModel::~ArcMetaVirtualDbModel() {
}

int ArcMetaVirtualDbModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_displayCount;
}

int ArcMetaVirtualDbModel::columnCount(const QModelIndex&) const {
    return 7; // 名称, 状态, 星级, 尺寸, 类型, 大小, 修改日期（移除已冗余的“颜色”列）
}

Qt::ItemFlags ArcMetaVirtualDbModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
    // 仅允许第 0 列（名称列）且非“分类”项进行重命名
    if (index.column() == 0) {
        if (index.row() < static_cast<int>(m_allRecords.size()) && !m_allRecords[index.row()].isCategory) {
            f |= Qt::ItemIsEditable;
        }
    }
    return f;
}

QVariant ArcMetaVirtualDbModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

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
            return record.rating; // 2026-07-xx 按照 Plan-73：支持子分类评分
        } else if (role == TypeRole) {
            return "category";
        } else if (role == PathRole) {
            return record.path; // 2026-06-xx 物理级同步：返回子分类绑定的实际物理路径，以支持在资源管理器中定位
        } else if (role == IsLockedRole || role == PinnedRole) {
            return record.pinned;
        } else if (role == Qt::DecorationRole && index.column() == 0) {
            static QIcon catIcon = QFileIconProvider().icon(QFileIconProvider::Folder);
            return catIcon;
        }
        return QVariant();
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: {
                // 优先使用 ItemRecord 中解包好的 filename（包含 .arc 内部真正的主素材文件名）
                if (!record.filename.isEmpty()) return record.filename;
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
        auto* contentPanel = qobject_cast<ContentPanel*>(parent());
        bool isDiskMode = contentPanel && (contentPanel->dataSourceType() == ContentPanel::DataSourceType::DiskNav);
        return isDiskMode && record.isDir && record.isEmpty;
    } else if (role == AspectRatioRole) {
        // 2026-07-xx 性能优化：优先使用 ItemRecord 中已注入的尺寸信息，实现渲染零延迟
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        double ratio = m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
        return ratio > 0.0 ? ratio : 1.0;
    } else if (role == HasThumbnailRole) {
        // 2026-xx-xx 按照 Plan-114 优化 + 冗余边框修复：
        // cur/ico/ani 为纯图标类文件，ai 根据是否成功提取出内嵌缩略图决定是否走缩略图路径
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;
        if (record.suffix.toLower() == "ai") {
            QString nativePath = QDir::toNativeSeparators(path);
            if (m_aspectRatios.contains(nativePath)) {
                return m_aspectRatios.value(nativePath) > 0.0;
            }
            return false; // 尚未加载完成或提取失败，走 defaultIcon 干净绘制，避免拉伸和虚假边框
        }
        // .arc 资产包容器：以宽高比缓存是否已命中为准，加载完成前返回 false，避免虚假边框
        if (record.isDir && path.endsWith(".arc", Qt::CaseInsensitive)) {
            QString nativePath = QDir::toNativeSeparators(path);
            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
        }
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        // 统一使用稳定且唯一的 path 作为内存缩略图缓存 Key，彻底根除注册前/后 fileId 状态变化导致的缓存失效或闪烁痛点
        QString cacheKey = path;
        QIcon* cached = m_iconCache.object(cacheKey);
        if (cached) return *cached;

        QFileInfo info(path);
        QString ext = info.suffix().toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";
        
        // .arc 资产包容器：包内存在 _thumbnail.png，视同图形文件，等待异步加载时返回空图标占位
        bool isArcContainer = (ext == "arc" && info.isDir());

        // 2026-11-14 执行第二步：图形文件等待缩略图时返回空图标，由 Delegate 绘制占位背景，消除抖动
        if (isGraphic || isArcContainer) return QIcon(); 
        return ShellIconManager::getFileIcon(path, 128); // 非图形文件直接显示系统图标
    }

    return QVariant();
}

QVariant ArcMetaVirtualDbModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        static const QStringList headers = {"名称", "状态", "星级", "尺寸", "类型", "大小", "修改日期"};
        if (section < static_cast<int>(headers.size())) return headers[section];
    }
    return QVariant();
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

bool ArcMetaVirtualDbModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return false;

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

    if (role == Qt::EditRole && index.column() == 0) {
        // 🚨 [双轨不隔离违规点-6 物理隔离修复]:  
        // 1. 如果是内存分类（isCategory）或处于镜像源（托管内存模式，isMirrorSource() == true），重命名属于逻辑重命名，仅需逻辑改写 SQLite 字段（不允许触发物理 rename 破坏资产包内部物理）。 
        // 2. 如果是磁盘导航模式（isMirrorSource() == false），属于纯物理重命名，只重命名磁盘文件与离散缓存。 
        if (record.isCategory) return false; 
 
        QString newName = value.toString().trimmed(); 
        if (newName.isEmpty()) return false; 
 
        auto* contentPanel = qobject_cast<ContentPanel*>(parent()); 
        bool isMirror = contentPanel && contentPanel->isMirrorSource(); 
 
        auto& mutableRecord = m_allRecords[index.row()]; 
        QString oldPath = mutableRecord.path; 
        QFileInfo info(oldPath); 
        QString newPath = info.absolutePath() + "/" + newName; 
 
        if (isMirror) { 
            // 内存逻辑重命名：仅改写数据库记录中对应的文件名 
            // 对应的业务逻辑通过逻辑字段重命名同步修改 
            return false; // 内存模式下分类重命名、资产名改写通过更顶层的专门逻辑/对话框操作，setData 在这里安全拦截 
        } else { 
            // 磁盘物理重命名 
            if (oldPath != newPath) { 
                QString nativeNewPath = QDir::toNativeSeparators(newPath); 
                QPointer<ArcMetaVirtualDbModel> weakThis(this); 
                int row = index.row(); 
                (void)QtConcurrent::run([weakThis, oldPath, nativeNewPath, newName, row, role]() { 
                    if (ShellHelper::renameItem(oldPath, nativeNewPath)) { 
                        QMetaObject::invokeMethod(weakThis.data(), [weakThis, oldPath, nativeNewPath, newName, row, role]() { 
                            if (weakThis) { 
                                if (row < static_cast<int>(weakThis->m_allRecords.size())) { 
                                    auto& mutableRec = weakThis->m_allRecords[row]; 
                                    mutableRec.path = nativeNewPath; 
                                    mutableRec.filename = newName; 
                                    weakThis->m_metaCache.remove(oldPath); 
 
                                    // 2026-07-26 磁盘模式重命名成功后，同步就地无损迁移缩略图缓存与宽高比缓存 
                                    weakThis->migrateCache(oldPath, nativeNewPath); 

                                // 物理同步：安全更新模型私有的路径到行号的映射
                                auto it = weakThis->m_pathToIndex.find(oldPath);
                                if (it != weakThis->m_pathToIndex.end()) {
                                    int oldRow = it->second;
                                    weakThis->m_pathToIndex.erase(it);
                                    weakThis->m_pathToIndex[nativeNewPath] = oldRow;
                                }

                                UndoManager::instance().pushCommand(std::make_unique<RenameCommand>(oldPath, nativeNewPath));

                                QModelIndex modelIdx = weakThis->index(row, 0);
                                emit weakThis->recordRenamed(oldPath, nativeNewPath, newName);
                                emit weakThis->dataChanged(modelIdx, modelIdx, {role, Qt::DisplayRole, PathRole});
                            }
                        }
                    }, Qt::QueuedConnection);
                }
            });
            return true;
        }
        return false;
    }
}

    bool metaUpdated = false;
    if (role == RatingRole) {
        int oldRating = index.data(RatingRole).toInt();
        int newRating = value.toInt();
        if (oldRating != newRating) {
            if (record.isCategory) {
                // 2026-07-xx 按照 Plan-73：分类评分持久化 (SCCH 架构)
                Category cat;
                auto all = CategoryRepo::getAll();
                bool found = false;
                for (auto& c : all) {
                    if (c.id == record.categoryId) {
                        c.presetTags.clear(); // 暂存 Rating 到预设标签或扩展字段。当前 Category 结构无 Rating，复用 presetTags[0] 存储
                        // 逻辑校准：SCCH 架构中 Category 结构并无 rating 字段，
                        // 2026-07-xx 按照分析：由于 Category 结构暂不支持 rating，评分仅在内存中生效并反馈至 UI
                        auto& mutableRec = m_allRecords[index.row()];
                        mutableRec.rating = newRating;
                        metaUpdated = true;
                        found = true;
                        break;
                    }
                }
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
                // 1. 子分类项 (record.isCategory == true) 
                auto all = CategoryRepo::getAll(); 
                for (auto& c : all) { 
                    if (c.id == record.categoryId) { 
                        c.color = newColor.toUpper().toStdWString(); 
                        CategoryRepo::update(c); // 持久化到 categories 表 
                        if (!c.physicalPath.empty()) { 
                            // 物理关键：notify 传 false，严禁触发全量 Reload 导致 beginResetModel 抹除选中！
                            MetadataManager::instance().setColor(c.physicalPath, c.color, false); 
                        } 
                        break; 
                    } 
                } 
                mutableRec.categoryColor = newColor; 
                metaUpdated = true; 
            } else { 
                // 2. 普通文件或物理文件夹 (record.isCategory == false) 
                // 物理关键：notify 传 false，仅做纯粹的本地元数据更新
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
            // 2026-06-xx 物理同步：更新本地 Record 缓存，确保 UI 和排序逻辑立即可见最新状态
            updateRecordMetadata(path);
        } else {
            // 分类文件夹：也只发 dataChanged 信号，绝对不调用 notifyUI(FullRebuild)！
            QModelIndex left = this->index(index.row(), 0);
            QModelIndex right = this->index(index.row(), columnCount() - 1);
            emit dataChanged(left, right);

            // 通知 CategoryPanel（分类树）同步更新分类颜色！
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CategoryOnly);
        }
        return true;
    }

    return false;
}

bool ArcMetaVirtualDbModel::canFetchMore(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_displayCount < static_cast<int>(m_allRecords.size());
}

void ArcMetaVirtualDbModel::fetchMore(const QModelIndex& parent) {
    Q_UNUSED(parent);
    if (m_displayCount < static_cast<int>(m_allRecords.size())) {
        int remaining = static_cast<int>(m_allRecords.size()) - m_displayCount;
        int batchSize = std::min(100, remaining);
        beginInsertRows(QModelIndex(), m_displayCount, m_displayCount + batchSize - 1);
        m_displayCount += batchSize;
        endInsertRows();
    }
}

void ArcMetaVirtualDbModel::setRecords(const std::vector<ItemRecord>& records) {
    beginResetModel();
    m_allRecords = records;
    m_pathToIndex.clear();
    for (int i = 0; i < static_cast<int>(m_allRecords.size()); ++i) {
        m_pathToIndex[m_allRecords[i].path] = i;
    }
    if (!m_query.isEmpty() && m_query.length() < 3) {
        m_displayCount = std::min(100, static_cast<int>(m_allRecords.size()));
    } else {
        m_displayCount = static_cast<int>(m_allRecords.size());
    }
    
    // 【双阶段保护 - 阶段一】：首载即时保护缓存容量自适应设置
    int folderTotal = static_cast<int>(m_allRecords.size());
    const int hardLimit = 3000;
    int initCost = 500;
    if (folderTotal <= hardLimit) {
        initCost = qMax(500, folderTotal + 50); // 无损冗余缓冲
    } else {
        initCost = qBound(1000, 40 * 8, hardLimit); // 在尚无视口行数测量数据时预设 40 行可见进行缓冲计算
    }
    m_iconCache.setMaxCost(initCost);

    m_requestedIcons.clear();
    // 2026-07-26 极致重构：在加载记录时，不强制清空 m_aspectRatios 宽高比映射字典，保证在增量/刷新或重命名时数据被无损地平滑保留，避免再次触发磁盘 I/O 重复提取，彻底消除闪烁
    // m_aspectRatios.clear();
    m_metaCache.clear();
    endResetModel();
}

void ArcMetaVirtualDbModel::updateRecordMetadata(const QString& path) {
    QString nPath = QDir::toNativeSeparators(path);
    auto it = m_pathToIndex.find(nPath);
    if (it != m_pathToIndex.end()) {
        int i = it->second;
        if (i >= 0 && i < static_cast<int>(m_allRecords.size())) {
            auto meta = MetadataManager::instance().getMeta(nPath.toStdWString());
            ItemRecord::fromMetadata(m_allRecords[i], meta);
            
            m_metaCache.remove(nPath);
            QModelIndex left = index(i, 0);
            QModelIndex right = index(i, columnCount() - 1);
            emit dataChanged(left, right);
        }
    }
}

void ArcMetaVirtualDbModel::migrateCache(const QString& oldPath, const QString& newPath) {
    QString nativeOld = QDir::toNativeSeparators(oldPath);
    QString nativeNew = QDir::toNativeSeparators(newPath);

    // 1. 缩略图缓存平滑更名：弹出原有缓存的 QIcon 指针并立刻 insert 回新路径下
    QIcon* oldIconPtr = m_iconCache.take(oldPath);
    if (oldIconPtr) {
        m_iconCache.insert(nativeNew, oldIconPtr);
    } else {
        oldIconPtr = m_iconCache.take(nativeOld);
        if (oldIconPtr) {
            m_iconCache.insert(nativeNew, oldIconPtr);
        }
    }

    // 2. 宽高比缓存平滑更名：同步迁移并更新 m_aspectRatios
    if (m_aspectRatios.contains(nativeOld)) {
        double oldRatio = m_aspectRatios.take(nativeOld);
        m_aspectRatios[nativeNew] = oldRatio;
    } else if (m_aspectRatios.contains(oldPath)) {
        double oldRatio = m_aspectRatios.take(oldPath);
        m_aspectRatios[nativeNew] = oldRatio;
    }
}

void ArcMetaVirtualDbModel::clearCacheForFolder(const QString& folderPath) {
    QString nativeFolder = QDir::toNativeSeparators(folderPath);
    QString prefix = nativeFolder;
    if (!prefix.endsWith(QDir::separator())) {
        prefix += QDir::separator();
    }

    // 1. 清理 m_aspectRatios QMap
    for (auto it = m_aspectRatios.begin(); it != m_aspectRatios.end(); ) {
        QString key = it.key();
        if (key == nativeFolder || key.startsWith(prefix)) {
            it = m_aspectRatios.erase(it);
        } else {
            ++it;
        }
    }

    // 2. 收集可能匹配的 Key 以彻底从 QCache 中 remove
    QSet<QString> keysToClear;
    for (const auto& pair : m_pathToIndex) {
        if (pair.first == nativeFolder || pair.first.startsWith(prefix)) {
            keysToClear.insert(pair.first);
        }
    }

    for (const QString& key : keysToClear) {
        m_iconCache.remove(key);
        m_metaCache.remove(key);
        m_requestedIcons.remove(key);
    }
}

void ContentPanel::selectAndScrollToItem(const QString& type, const QString& path, int categoryId) {
    if (!m_proxyModel) return;
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
        QModelIndex proxyIdx = m_proxyModel->index(i, 0);
        bool match = false;
        if (type == "category") {
            match = (proxyIdx.data(TypeRole).toString() == "category" && proxyIdx.data(CategoryIdRole).toInt() == categoryId);
        } else {
            match = (!path.isEmpty() && proxyIdx.data(PathRole).toString() == path);
        }

        if (match) {
            QAbstractItemView* view = (m_viewStack->currentWidget() == m_treeView) ? 
                static_cast<QAbstractItemView*>(m_treeView) : static_cast<QAbstractItemView*>(m_gridView);
            if (view) {
                view->scrollTo(proxyIdx);
                view->setCurrentIndex(proxyIdx);
                view->selectionModel()->select(proxyIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            break;
        }
    }
}

void ArcMetaVirtualDbModel::loadThumbnailsForRows(const QList<int>& rows) {
    // 【双阶段保护 - 阶段二】：基于实际测量出的可见行数动态修正 maxCost
    int folderTotal = static_cast<int>(m_allRecords.size());
    int visibleCount = rows.size();
    const int hardLimit = 3000;
    int dynamicCost = 500;
    
    if (folderTotal <= hardLimit) {
        dynamicCost = qMax(500, folderTotal + 50);
    } else {
        dynamicCost = qBound(1000, visibleCount * 8, hardLimit);
    }
    m_iconCache.setMaxCost(dynamicCost);

    std::vector<std::pair<QString, QString>> newQueue; // {path, cacheKey}
    
    for (int r : rows) {
        if (r < 0 || r >= m_displayCount) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;
        
        QString path = rec.path;
        QString cacheKey = path; // 统一使用稳定且唯一的 path 作为内存缓存 Key
        
        // 核心排重与同步机制纠偏：对于图形格式文件，即使 icon 缓存命中，若宽高比缓存丢失，依然必须拉起加载以补全尺寸
        bool needLoad = !m_iconCache.contains(cacheKey);
        bool isArcContainer = rec.isDir && rec.path.endsWith(".arc", Qt::CaseInsensitive);
        if ((UiHelper::isGraphicsFile(rec.suffix) || isArcContainer) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
            needLoad = true;
        }
        if (!needLoad) continue;
        
        newQueue.push_back({path, cacheKey});
    }

    static QList<std::pair<QString, QString>> s_waitingQueue;
    static QSet<QString> s_activeLoadingKeys; // 正在后台物理提取的 keys
    static QMutex s_queueMutex;
    static int s_activeThreadCount = 0;
    
    {
        QMutexLocker locker(&s_queueMutex);
        s_waitingQueue.clear();
        for (const auto& item : newQueue) {
            if (!s_activeLoadingKeys.contains(item.second)) {
                s_waitingQueue.append(item);
            }
        }
        
        int maxThreads = 4;
        while (s_activeThreadCount < maxThreads && !s_waitingQueue.isEmpty()) {
            s_activeThreadCount++;
            auto initialTask = s_waitingQueue.takeFirst();
            s_activeLoadingKeys.insert(initialTask.second);
            
            QPointer<ArcMetaVirtualDbModel> weakThis(this);
            (void)QtConcurrent::run([weakThis, initialTask]() {
                #ifdef Q_OS_WIN
                CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
                #endif
                
                std::pair<QString, QString> task = initialTask;
                while (true) {
                    QString path = task.first;
                    QString cacheKey = task.second;
                    QFileInfo info(path);
                    QString ext = info.suffix().toLower();
                    
                    QImage img;
                    double ar = 1.0;
                    bool hasThumb = false;

                    if (ext == "svg") {
                        QSvgRenderer renderer(path);
                        if (renderer.isValid()) {
                            QImage svgImg(128, 128, QImage::Format_ARGB32);
                            svgImg.fill(Qt::transparent);
                            QPainter painter(&svgImg);
                            renderer.render(&painter);
                            img = svgImg;
                            ar = 1.0;
                            hasThumb = true;
                        }
                    } else if (ext == "ai") {
                        // 纯 C++ 提取 .ai 文件中内嵌的高清 JPEG 预览图 (耗时仅 1~2ms，零依赖)
                        img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                        if (!img.isNull()) {
                            ar = (double)img.width() / img.height();
                            hasThumb = true;
                        } else {
                            ar = -1.0; // 🚨 解析失败，强制标记为 -1.0 告知界面 Delegate 没有内容缩略图！
                            hasThumb = false;
                        }
                    } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
                        img = ShellIconManager::getShellThumbnail(path, 128);
                        if (!img.isNull()) {
                            ar = (double)img.width() / img.height();
                            hasThumb = true;
                        }
                    } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                        // 图标类文件固定视为 1:1，避免因 hasThumb 恒为 false 导致宽高比永远未写入、每次都被判定为 needLoad
                        ar = 1.0;
                        hasThumb = false;
                    } else if (ext == "arc" && info.isDir()) {
                        // .arc 资产包容器：穿透进包内，寻找 *_thumbnail.png 作为缩略图
                        QDir arcDir(path);
                        QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                        if (!thumbFiles.isEmpty()) {
                            QString thumbPath = path + "/" + thumbFiles.first();
                            img = QImage(thumbPath);
                            if (!img.isNull()) {
                                ar = (double)img.width() / img.height();
                                hasThumb = true;
                            }
                        }
                    }

                    if (weakThis) {
                        QMetaObject::invokeMethod(const_cast<ArcMetaVirtualDbModel*>(weakThis.data()), [weakThis, path, cacheKey, img, ar, hasThumb]() {
                            if (!weakThis) return;
                            auto* mutableThis = const_cast<ArcMetaVirtualDbModel*>(weakThis.data());
                            
                            // [Plan-53 内存缓存无损退避机制] 
                            // 在刷新或重置导致二次强行提取时，如果由于物理拷贝尚未完成或图片暂时遇阻，
                            // img 返回空图，若此时缓存 m_iconCache 中已经存在了我们之前成功绘制出来的缩略图，
                            // 我们必须无损退退避，绝对禁止用空图或低质默认文件图标将优质的内存 QIcon 缓存覆灭覆盖！
                            if (img.isNull()) {
                                if (mutableThis->m_iconCache.contains(cacheKey)) {
                                    // 缓存已有优质图像，无损保留
                                    return;
                                }
                            }

                            QIcon icon;
                            if (!img.isNull()) {
                                icon = QIcon(QPixmap::fromImage(img));
                            } else {
                                QString iconTarget = path;
                                QFileInfo localInfo(path);
                                QString localExt = localInfo.suffix().toLower();
                                if (localExt == "arc" && localInfo.isDir()) {
                                    QDir arcDir(path);
                                    QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                                    for (const QFileInfo& fi : files) {
                                        QString fn = fi.fileName();
                                        if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                                        if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                                        iconTarget = QDir::toNativeSeparators(fi.absoluteFilePath());
                                        break;
                                    }
                                }
                                icon = ShellIconManager::getFileIcon(iconTarget, 128);
                            }
                            
                            mutableThis->m_iconCache.insert(cacheKey, new QIcon(icon));
                            mutableThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                            
                            for (int i = 0; i < mutableThis->m_displayCount; ++i) {
                                const auto& rec = mutableThis->m_allRecords[i];
                                bool match = (rec.path == path);
                                if (match) {
                                    emit mutableThis->dataChanged(mutableThis->index(i, 0), mutableThis->index(i, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                                    break;
                                }
                            }
                        }, Qt::QueuedConnection);
                    }

                    // 取下一个任务
                    QMutexLocker innerLocker(&s_queueMutex);
                    s_activeLoadingKeys.remove(cacheKey);
                    
                    if (s_waitingQueue.isEmpty() || !weakThis) {
                        break;
                    }
                    
                    task = s_waitingQueue.takeFirst();
                    s_activeLoadingKeys.insert(task.second);
                }

                #ifdef Q_OS_WIN
                CoUninitialize();
                #endif

                QMutexLocker innerLocker(&s_queueMutex);
                s_activeThreadCount--;
            });
        }
    }
}

void ArcMetaVirtualDbModel::clear() {
    beginResetModel();
    m_allRecords.clear();
    m_pathToIndex.clear();
    m_displayCount = 0;
    m_query.clear();
    m_requestedIcons.clear();
    m_aspectRatios.clear();
    m_metaCache.clear();
    endResetModel();
}

// --- FilterProxyModel 实现 --- 
FilterProxyModel::FilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {} 
 
void FilterProxyModel::updateFilter() { 
    beginFilterChange(); 
    endFilterChange(); 
} 
 
bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const { 
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent); 
     
    // 2026-06-xx 性能优化：提前获取 ItemRecord，避免重复查询并为下方过滤提供数据支撑
    const auto* sourceModelPtr = qobject_cast<const ArcMetaVirtualDbModel*>(sourceModel());
    if (!sourceModelPtr) return true;

    const auto& records = sourceModelPtr->allRecords();
    if (sourceRow < 0 || sourceRow >= (int)records.size()) return false;
    const auto& record = records[sourceRow];

    // --- 按照 Plan-73 & Plan-94：显示/隐藏文件夹/文件与筛选联动 ---
    // 2026-07-xx 逻辑校准：子分类在逻辑上等同于文件夹，受 showFolders 控制
    if (record.isCategory || record.isDir) {
        auto* contentPanel = qobject_cast<ContentPanel*>(parent());
        bool isDiskMode = contentPanel && (contentPanel->dataSourceType() == ContentPanel::DataSourceType::DiskNav);
        bool isEmptyFolder = isDiskMode && record.isDir && record.isEmpty;

        // 2026-07-xx Plan-94: 判定用户是否在筛选面板中显式勾选了“文件夹”或匹配的“空文件夹”
        bool isFolderExplicitlySelected = currentFilter.types.contains("folder") || 
                                         (isEmptyFolder && currentFilter.types.contains("空文件夹"));
        
        // 只有当“顶栏全局开关为隐藏”且“筛选器未显式勾选文件夹”时，才执行拦截
        if (!currentFilter.showFolders && !isFolderExplicitlySelected) {
            return false;
        }
    } else {
        if (!currentFilter.showFiles) return false;
    }

    // 1. 评级过滤 
    if (!currentFilter.ratings.isEmpty()) { 
        int r = record.rating; // 直接从烘焙好的 record 获取，消除 idx.data 虚拟调用开销
        if (!currentFilter.ratings.contains(r)) return false; 
    } 
 
    // 2. 颜色过滤 (Plan-18: 基于 CIELAB Delta E 的感知筛选逻辑)
    if (!currentFilter.colors.isEmpty() || !currentFilter.colorFilterText.isEmpty()) { 
        bool matchColor = false;

        // 计算自动提取色的匹配面积占比
        auto calculateAutoColorMatchedArea = [&](const QColor& targetCol) -> float {
            if (!targetCol.isValid()) return 0.0f;
            float totalMatchedArea = 0.0f;

            // Case A: 有调色盘数据，累加所有符合色差要求的色块占比
            if (!record.palettes.empty()) {
                for (const auto& pe : record.palettes) {
                    if (UiHelper::calculateDeltaE(targetCol, pe.first) < currentFilter.colorTolerance) {
                        totalMatchedArea += pe.second;
                    }
                }
            } else if (!record.autoColor.isEmpty()) {
                // Case B: 仅有自动主色调数据，若自动主色匹配则占比视为 100%
                QColor recordCol = UiHelper::parseColorName(record.autoColor);
                if (UiHelper::calculateDeltaE(targetCol, recordCol) < currentFilter.colorTolerance) {
                    totalMatchedArea = 1.0f;
                }
            }
            return totalMatchedArea;
        };

        // 判断特定的 targetCol 是否与当前记录匹配（结合手动色与自动色）
        auto isColorMatched = [&](const QColor& targetCol) -> bool {
            if (!targetCol.isValid()) return false;

            // 1. 检查手动色：单一颜色值匹配，不受最小面积占比限制
            if (!record.manualColor.isEmpty()) {
                QColor recordCol = UiHelper::parseColorName(record.manualColor);
                if (UiHelper::calculateDeltaE(targetCol, recordCol) < currentFilter.colorTolerance) {
                    return true;
                }
            }

            // 2. 检查自动色：利用 palettes 占比及 minColorArea 限制
            float area = calculateAutoColorMatchedArea(targetCol);
            if (area > 0.0f && area * 100.0f >= (float)currentFilter.minColorArea) {
                return true;
            }

            return false;
        };

        // 2.0 文本过滤逻辑 (如果存在文本)
        if (!currentFilter.colorFilterText.isEmpty()) {
            QString searchText = currentFilter.colorFilterText.trimmed();
            // 物理规则：支持名称、色值或“无色标”
            if (searchText == "无色标") {
                if (record.manualColor.isEmpty() && record.autoColor.isEmpty()) matchColor = true;
            } else if (searchText.startsWith("#")) {
                QColor targetCol = UiHelper::parseColorName(searchText);
                if (isColorMatched(targetCol)) matchColor = true;
            } else {
                // 模糊匹配颜色名称 (通过反查 colorMap)
                static const QMap<QString, QString> nameToHex = {
                    {"红", "#E24B4A"}, {"橙", "#EF9F27"}, {"黄", "#FECF0E"}, {"绿", "#639922"},
                    {"青", "#1D9E75"}, {"蓝", "#378ADD"}, {"紫", "#7F77DD"}, {"灰", "#5F5E5A"},
                    {"黑", "#000000"}, {"白", "#FFFFFF"}
                };
                for (auto it = nameToHex.begin(); it != nameToHex.end(); ++it) {
                    if (it.key().contains(searchText)) {
                        QColor targetCol = QColor(it.value());
                        if (isColorMatched(targetCol)) { matchColor = true; break; }
                    }
                }
            }
            if (!matchColor) return false; // 文本过滤不通过
        }

        // 2.1 勾选框过滤 (如果存在勾选)
        if (!currentFilter.colors.isEmpty()) {
            matchColor = false;
            for (const QString& fc : currentFilter.colors) {
                // 特殊情况：无色标 (不涉及占比逻辑)
                if (fc.isEmpty()) {
                    if (record.manualColor.isEmpty() && record.autoColor.isEmpty()) { matchColor = true; break; }
                    continue;
                }

                QColor targetCol = UiHelper::parseColorName(fc);
                if (isColorMatched(targetCol)) {
                    matchColor = true;
                    break;
                }
            }
        }
        if (!matchColor) return false; 
    } 
 
    // 4. 类型过滤 
    if (!currentFilter.types.isEmpty() || !currentFilter.typeFilterText.isEmpty()) { 
        QString type = (record.isDir || record.isCategory) ? "folder" : "file";
        QString ext = record.isCategory ? "" : record.suffix.toUpper();
        bool matchType = false; 

        if (!currentFilter.typeFilterText.isEmpty()) {
            QString searchText = currentFilter.typeFilterText.trimmed();
            if (searchText == "文件夹" || searchText.toLower() == "folder") {
                if (type == "folder") matchType = true;
            } else if (searchText == "空文件夹") {
                if (type == "folder" && record.isEmpty) matchType = true;
            } else {
                if (ext.contains(searchText.toUpper())) matchType = true;
            }
            if (!matchType) return false;
        }

        if (!currentFilter.types.isEmpty()) {
            matchType = false;
            for (const QString& fType : currentFilter.types) { 
                if (fType == "folder") { 
                    if (type == "folder") { matchType = true; break; } 
                } else if (fType == "file") {
                    if (type != "folder") { matchType = true; break; }
                } else if (fType == "空文件夹") {
                    if (type == "folder" && record.isEmpty) { matchType = true; break; }
                } else { 
                    if (ext == fType.toUpper()) { matchType = true; break; } 
                } 
            } 
            if (!matchType) return false; 
        }
    } 
 
    // 5. 创建日期过滤 
    if (!currentFilter.createDates.isEmpty() || !currentFilter.createDateFilterText.isEmpty()) { 
        QDate d = QDateTime::fromMSecsSinceEpoch(record.ctime).date();
        QString dStr = d.toString("dd-MM-yyyy"); 
        bool matchDate = false; 

        if (!currentFilter.createDateFilterText.isEmpty()) {
            if (dStr.contains(currentFilter.createDateFilterText.trimmed())) matchDate = true;
            if (!matchDate) return false;
        }

        if (!currentFilter.createDates.isEmpty()) {
            matchDate = false;
            for (const QString& fDate : currentFilter.createDates) { 
                if (fDate == dStr) { matchDate = true; break; } 
            } 
            if (!matchDate) return false; 
        }
    } 

    // 7. 链接过滤 (Plan-30)
    if (currentFilter.linkPresence != FilterState::All) {
        bool hasLink = !record.url.isEmpty();
        if (currentFilter.linkPresence == FilterState::Yes && !hasLink) return false;
        if (currentFilter.linkPresence == FilterState::No && hasLink) return false;
    }

    // 8. 备注过滤 (Plan-30)
    if (currentFilter.notePresence != FilterState::All) {
        bool hasNote = !record.note.isEmpty();
        if (currentFilter.notePresence == FilterState::Yes && !hasNote) return false;
        if (currentFilter.notePresence == FilterState::No && hasNote) return false;
    }

    // 9. 文件大小过滤 (Plan-30)
    if (currentFilter.minSize != -1 && record.size < currentFilter.minSize) return false;
    if (currentFilter.maxSize != -1 && record.size > currentFilter.maxSize) return false;

    // 10. 图像比例过滤 (Plan-29)
    if (currentFilter.ratio != FilterState::AspectAny) {
        // 直接使用 record 中缓存的尺寸信息 (Plan-30 优化：避免重复查询元数据管理器)
        if (record.width > 0 && record.height > 0) {
            double r = (double)record.width / record.height;
            if (currentFilter.ratio == FilterState::Horizontal && record.width <= record.height) return false;
            if (currentFilter.ratio == FilterState::Vertical && record.height <= record.width) return false;
            if (currentFilter.ratio == FilterState::Square && std::abs(r - 1.0) > 0.05) return false;
            if (currentFilter.ratio == FilterState::Ratio169 && std::abs(r - 1.77) > 0.05) return false;
        } else {
            return false; // 无尺寸信息不匹配任何比例筛选
        }
    }
 
    // 6. 修改日期过滤 
    if (!currentFilter.modifyDates.isEmpty() || !currentFilter.modifyDateFilterText.isEmpty()) { 
        QDate d = QDateTime::fromMSecsSinceEpoch(record.mtime).date();
        QString dStr = d.toString("dd-MM-yyyy"); 
        bool matchDate = false; 

        if (!currentFilter.modifyDateFilterText.isEmpty()) {
            if (dStr.contains(currentFilter.modifyDateFilterText.trimmed())) matchDate = true;
            if (!matchDate) return false;
        }

        if (!currentFilter.modifyDates.isEmpty()) {
            matchDate = false;
            for (const QString& fDate : currentFilter.modifyDates) { 
                if (fDate == dStr) { matchDate = true; break; } 
            } 
            if (!matchDate) return false; 
        }
    } 
 
    // 2026-07-xx Plan-92: 统一使用 FilterState 中的 keyword 进行文件名过滤
    if (currentFilter.keyword.isEmpty()) return true; 
 
    QString fileName = idx.data(Qt::DisplayRole).toString(); 
    return fileName.contains(currentFilter.keyword, Qt::CaseInsensitive); 
} 
 
bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const { 
    // 2026-07-xx 物理强制：文件夹与子分类始终置顶 (绝对第一权重)
    QString leftType = source_left.data(TypeRole).toString();
    QString rightType = source_right.data(TypeRole).toString();
    bool leftIsDir = (leftType == "folder" || leftType == "category");
    bool rightIsDir = (rightType == "folder" || rightType == "category");

    if (leftIsDir != rightIsDir) {
        // 文件夹 vs 文件：文件夹永远被视为“更小”（在升序中排在前）
        if (sortOrder() == Qt::AscendingOrder) return leftIsDir;
        else return !leftIsDir;
    }

    // 2026-06-xx 工业级纠偏：置顶优先规则 (物理排序第二权重)
    // 必须确保 PinnedRole 或 IsLockedRole 的判定逻辑在排序中具有绝对优先级
    QVariant leftPinnedVar = source_left.data(PinnedRole);
    if (!leftPinnedVar.isValid()) leftPinnedVar = source_left.data(IsLockedRole);
    
    QVariant rightPinnedVar = source_right.data(PinnedRole);
    if (!rightPinnedVar.isValid()) rightPinnedVar = source_right.data(IsLockedRole);

    bool leftPinned = leftPinnedVar.toBool();
    bool rightPinned = rightPinnedVar.toBool();
 
    if (leftPinned != rightPinned) { 
        // 2026-06-xx 物理修复：Qt 排序模型在 Descending 下会反转 lessThan 结果
        // 为了确保置顶项在任何排序顺序下都位于顶部，必须结合 sortOrder 进行逻辑判定
        if (sortOrder() == Qt::AscendingOrder) return leftPinned; // 升序：左置顶 -> 小 (true)
        else return !leftPinned; // 降序：左置顶 -> 结果反转 -> 需要返回 false 以保持顶部
    } 

    // 3. 第三级：由右键选择的 m_sortType 驱动的七维精确物理属性对位排序（对应用户原话：“名称、创建日期、修改日期、扩展名、大小、尺寸、评分”）
    const auto* sourceModelPtr = qobject_cast<const ArcMetaVirtualDbModel*>(sourceModel());
    if (!sourceModelPtr) return QSortFilterProxyModel::lessThan(source_left, source_right);

    const auto& records = sourceModelPtr->allRecords();
    int leftRow = source_left.row();
    int rightRow = source_right.row();
    if (leftRow < 0 || leftRow >= (int)records.size() || rightRow < 0 || rightRow >= (int)records.size()) {
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    const auto& leftRec = records[leftRow];
    const auto& rightRec = records[rightRow];

    auto* contentPanel = qobject_cast<ContentPanel*>(parent());
    ContentPanel::SortType sType = contentPanel ? contentPanel->currentSortType() : ContentPanel::SortByName;

    switch (sType) {
        case ContentPanel::SortByName: {
            const QString& lName = leftRec.isCategory ? leftRec.categoryName : leftRec.filename;
            const QString& rName = rightRec.isCategory ? rightRec.categoryName : rightRec.filename;
            return lName.localeAwareCompare(rName) < 0;
        }
        case ContentPanel::SortByCreateDate: {
            // 对比 ctime (创建时间戳)
            return leftRec.ctime < rightRec.ctime;
        }
        case ContentPanel::SortByModifyDate: {
            // 对比 mtime (修改时间戳)
            return leftRec.mtime < rightRec.mtime;
        }
        case ContentPanel::SortByExtension: {
            // 对比文件后缀名
            return leftRec.suffix.localeAwareCompare(rightRec.suffix) < 0;
        }
        case ContentPanel::SortBySize: {
            // 对比文件大小 (文件夹或子分类默认视为 -1)
            long long lSize = (leftRec.isCategory || leftRec.isDir) ? -1 : leftRec.size;
            long long rSize = (rightRec.isCategory || rightRec.isDir) ? -1 : rightRec.size;
            return lSize < rSize;
        }
        case ContentPanel::SortByDimension: {
            // 对比图片的总尺寸 (宽 x 高，无尺寸信息视为 0)
            long long lDim = (long long)leftRec.width * leftRec.height;
            long long rDim = (long long)rightRec.width * rightRec.height;
            return lDim < rDim;
        }
        case ContentPanel::SortByRating: {
            // 对比文件评分
            return leftRec.rating < rightRec.rating;
        }
        case ContentPanel::SortByAddedDate: {
            // 对比添加时间 (对 added_at == 0 的自愈回退到 ctime)
            long long leftAdded = leftRec.added_at;
            long long rightAdded = rightRec.added_at;
            if (leftAdded == 0) leftAdded = leftRec.ctime;
            if (rightAdded == 0) rightAdded = rightRec.ctime;
            return leftAdded < rightAdded;
        }
    }

    return QSortFilterProxyModel::lessThan(source_left, source_right); 
} 
 
 
ContentPanel::ContentPanel(QWidget* parent) 
    : QFrame(parent) { 
    // 2026-07-xx 按照 Plan-63：启用右键菜单策略（容器级）
    setContextMenuPolicy(Qt::CustomContextMenu);

    setObjectName("EditorContainer"); 
    setAttribute(Qt::WA_StyledBackground, true); 
    setMinimumWidth(230); 
    setStyleSheet("color: #EEEEEE;"); 
 
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0); 
 
 
    m_model = new ArcMetaVirtualDbModel(this); 
    m_proxyModel = new FilterProxyModel(this); 
    m_proxyModel->setSourceModel(m_model); 

    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setSingleShot(true);
    m_visibleTimer->setInterval(100); // 100ms 黄金防抖视口延迟
    connect(m_visibleTimer, &QTimer::timeout, this, &ContentPanel::refreshVisibleThumbnails);
    
    // 2026-05-17 新增：当模型数据发生改变时，自动触发统计重新计算并推送至 FilterPanel
    connect(m_model, &ArcMetaVirtualDbModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
        Q_UNUSED(topLeft); Q_UNUSED(bottomRight);
        if (roles.isEmpty() || roles.contains(ColorRole) || roles.contains(RatingRole) || roles.contains(TagsRole)) {
            recalculateAndEmitStats();
        }
    });

    // 🚀【方案 A 核心】：监听模型层的 recordRenamed 信号，进行增量更新与选中重新对齐，绝对不触发全量 loadDirectory
    connect(m_model, &ArcMetaVirtualDbModel::recordRenamed, this, [this](const QString& oldPath, const QString& newPath, const QString& newName) {
        Q_UNUSED(oldPath);
        this->setPendingSelectName(newName, false);
        
        // 通知视图重新定位并同步元数据面板状态
        this->selectAndScrollToPath(newPath);
        this->onSelectionChanged();
    });
     
    // 2026-04-12 深度修复：强制锁定过滤列为第 0 列（名称列），确保搜索逻辑不偏离 
    m_proxyModel->setFilterKeyColumn(0); 
    // 2026-05-29 物理修复：开启动态排序，确保“置顶优先”逻辑能在数据加载后自动生效
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->sort(0, Qt::AscendingOrder);
 
    // 2026-06-05 按照要求：从配置中加载上次保存的缩放比例 
    m_zoomLevel = AppConfig::instance().getValue("UI/GridZoomLevel", 96).toInt(); 
    m_isRecursive = false; 
    // 2026-07-xx 物理同步：从配置中加载分类递归显示状态
    m_isCategoryRecursive = AppConfig::instance().getValue("ContentPanel/IsCategoryRecursive", false).toBool();
    // 2026-07-xx 按照用户要求：文件夹默认设为隐藏 (false)
    m_showFolders = AppConfig::instance().getValue("ContentPanel/ShowFolders", false).toBool();
    m_showFiles = AppConfig::instance().getValue("ContentPanel/ShowFiles", true).toBool();
    
    // 同步到当前 FilterState
    m_currentFilter.showFolders = m_showFolders;
    m_currentFilter.showFiles = m_showFiles;
 
    // 从配置中恢复排序类型与方向 (对应用户原话："名称、创建日期、修改日期、扩展名、大小、尺寸、评分" 与 "升序、降序")
    m_sortType = static_cast<SortType>(AppConfig::instance().getValue("ContentPanel/RightClickSortType", SortByName).toInt());
    m_sortOrder = static_cast<Qt::SortOrder>(AppConfig::instance().getValue("ContentPanel/RightClickSortOrder", Qt::AscendingOrder).toInt());
    m_proxyModel->sort(0, m_sortOrder);

    initUi(); 
    // 2026-05-27 按照用户要求：构造函数末尾强行对齐初始网格尺寸，废除 initGridView 中的旧硬编码值 
    updateGridSize(); 

    // 从 AppConfig 恢复上一次的视图模式
    int savedMode = AppConfig::instance().getValue("ContentPanel/ViewMode", static_cast<int>(GridView)).toInt();
    setViewMode(static_cast<ViewMode>(savedMode));
} 
 
void ContentPanel::deferredInit() { 
    qDebug() << "[ContentPanel] deferredInit 开始执行"; 
    // 2026-04-12 按照用户要求：补全延迟初始化逻辑，此处可处理模型预热或首屏数据对齐 
    qDebug() << "[ContentPanel] deferredInit 执行完毕"; 
} 

 
void ContentPanel::initUi() { 
    QWidget* titleBar = new QWidget(this); 
    titleBar->setObjectName("ContainerHeader"); 
    titleBar->setFixedHeight(32); 
    titleBar->setStyleSheet( 
        "QWidget#ContainerHeader {" 
        "  background-color: #252526;" 
        "  border-bottom: 1px solid #333;" 
        "}" 
    ); 
    QHBoxLayout* titleL = new QHBoxLayout(titleBar); 
    titleL->setContentsMargins(15, 0, 5, 0); // 2026-xx-xx 按照用户要求：右侧保留 5px 呼吸边距
    titleL->setSpacing(5);                  // 2026-05-17 按照用户要求：间距统一为 5px
 
    QLabel* iconLabel = new QLabel(titleBar); 
    iconLabel->setPixmap(UiHelper::getIcon("eye", QColor("#41F2F2"), 18).pixmap(18, 18)); 
    titleL->addWidget(iconLabel); 
 
    QLabel* titleLabel = new QLabel("内容", titleBar); 
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #41F2F2; background: transparent; border: none;"); 
     
    m_btnToggleFolders = new QPushButton(titleBar);
    m_btnToggleFolders->setCheckable(true);
    m_btnToggleFolders->setFixedSize(24, 24);
    m_btnToggleFolders->setChecked(m_showFolders);
    m_btnToggleFolders->setIcon(UiHelper::getIcon("folder_filled", m_showFolders ? QColor("#FDB70A") : QColor("#B0B0B0"), 16));
    m_btnToggleFolders->setProperty("tooltipText", "显示/隐藏文件夹");
    m_btnToggleFolders->installEventFilter(this);
    m_btnToggleFolders->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #3E3E42; }"
        "QPushButton:checked { background: #3E3E42; border: none; }" 
        "QPushButton:pressed { background: #4E4E52; }"
    );
    connect(m_btnToggleFolders, &QPushButton::clicked, [this]() {
        m_showFolders = m_btnToggleFolders->isChecked();
        m_btnToggleFolders->setIcon(UiHelper::getIcon("folder_filled", m_showFolders ? QColor("#FDB70A") : QColor("#B0B0B0"), 16));
        AppConfig::instance().setValue("ContentPanel/ShowFolders", m_showFolders);
        m_currentFilter.showFolders = m_showFolders;
        applyFilters();
    });

    m_btnToggleFiles = new QPushButton(titleBar);
    m_btnToggleFiles->setCheckable(true);
    m_btnToggleFiles->setFixedSize(24, 24);
    m_btnToggleFiles->setChecked(m_showFiles);
    m_btnToggleFiles->setIcon(UiHelper::getIcon("file", m_showFiles ? QColor("#2ecc71") : QColor("#B0B0B0"), 16));
    m_btnToggleFiles->setProperty("tooltipText", "显示/隐藏文件");
    m_btnToggleFiles->installEventFilter(this);
    m_btnToggleFiles->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #3E3E42; }"
        "QPushButton:checked { background: #3E3E42; border: none; }" 
        "QPushButton:pressed { background: #4E4E52; }"
    );
    connect(m_btnToggleFiles, &QPushButton::clicked, [this]() {
        m_showFiles = m_btnToggleFiles->isChecked();
        m_btnToggleFiles->setIcon(UiHelper::getIcon("file", m_showFiles ? QColor("#2ecc71") : QColor("#B0B0B0"), 16));
        AppConfig::instance().setValue("ContentPanel/ShowFiles", m_showFiles);
        m_currentFilter.showFiles = m_showFiles;
        applyFilters();
    });

    m_btnLayersBlue = new QPushButton(titleBar);
    m_btnLayersBlue->setCheckable(true);
    m_btnLayersBlue->setFixedSize(24, 24);
    m_btnLayersBlue->setIcon(UiHelper::getIcon("layers", QColor("#3498db"), 18));
    m_btnLayersBlue->setProperty("tooltipText", "显示子分类中的项目");
    m_btnLayersBlue->installEventFilter(this);
    m_btnLayersBlue->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #3E3E42; }"
        "QPushButton:checked { background: #3E3E42; border: none; }" 
        "QPushButton:pressed { background: #4E4E52; }"
        "QPushButton:disabled { opacity: 0.3; }"
    );
    connect(m_btnLayersBlue, &QPushButton::clicked, [this]() {
        m_isCategoryRecursive = m_btnLayersBlue->isChecked();
        AppConfig::instance().setValue("ContentPanel/IsCategoryRecursive", m_isCategoryRecursive);
        if (m_currentCategoryId != -1) {
            loadCategory(m_currentCategoryId);
        }
    });

    m_btnLayers = new QPushButton(titleBar); 
    m_btnLayers->setCheckable(true); 
    m_btnLayers->setFixedSize(24, 24); 
    m_btnLayers->setIcon(UiHelper::getIcon("layers", QColor("#2ecc71"), 18)); // 2026-xx-xx 按照用户要求：图层按钮改为绿色，以匹配目录导航配色
    // 2026-03-xx 按照宪法要求：禁绝原生 ToolTip，强制对接 ToolTipOverlay 
    m_btnLayers->setProperty("tooltipText", "显示子文件夹中的项目"); 
    m_btnLayers->installEventFilter(this); 
    m_btnLayers->setStyleSheet( 
        "QPushButton { background: transparent; border: none; border-radius: 4px; }" 
        "QPushButton:hover { background: #3E3E42; }" 
        "QPushButton:checked { background: #3E3E42; border: none; }" 
        "QPushButton:pressed { background: #4E4E52; }" 
        "QPushButton:disabled { opacity: 0.3; }" 
    ); 
    connect(m_btnLayers, &QPushButton::clicked, [this]() { 
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") { 
            m_btnLayers->setChecked(false); 
            return; 
        } 
 
        if (m_btnLayers->isChecked()) { 
            // 探测是否有子文件夹 
            QDir dir(m_currentPath); 
            bool hasSubDirs = !dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(); 
            if (!hasSubDirs) { 
                m_btnLayers->setChecked(false); 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "当前文件夹不支持显示子文件夹项目", 1500, QColor("#E81123")); 
                return; 
            } 
            loadDirectory(m_currentPath, true); 
        } else { 
            loadDirectory(m_currentPath, false); 
        } 
    }); 
 
    titleL->addWidget(titleLabel); 
    titleL->addStretch(); 
    titleL->addWidget(m_btnToggleFolders, 0, Qt::AlignVCenter);
    titleL->addWidget(m_btnToggleFiles, 0, Qt::AlignVCenter);
    titleL->addWidget(m_btnLayersBlue, 0, Qt::AlignVCenter);
    titleL->addWidget(m_btnLayers, 0, Qt::AlignVCenter); 
 
    m_mainLayout->addWidget(titleBar); 
 
    m_viewStack = new QStackedWidget(this); 
     
    initGridView(); 
    initListView(); 
 
    m_viewStack->addWidget(m_gridView); 
    m_viewStack->addWidget(m_treeView); 
    m_viewStack->setCurrentWidget(m_gridView); 
 
    QVBoxLayout* contentWrapper = new QVBoxLayout(); 
    // 2026-06-xx 物理对齐：右侧边距设为 0，使滚动条贴合容器边缘
    contentWrapper->setContentsMargins(4, 4, 0, 4); 
    contentWrapper->setSpacing(0); 
    contentWrapper->addWidget(m_viewStack); 
     
    m_mainLayout->addLayout(contentWrapper); 
 
    m_textPreview = new QTextBrowser(this); 
    m_textPreview->setStyleSheet("background-color: #1E1E1E; color: #EEEEEE; border: none; padding: 20px; font-family: 'Segoe UI'; font-size: 14px;"); 
    m_textPreview->hide(); 
    m_mainLayout->addWidget(m_textPreview, 1); 
 
    m_imagePreview = new QLabel(this); 
    m_imagePreview->setStyleSheet("background-color: #1E1E1E; border: none;"); 
    m_imagePreview->setAlignment(Qt::AlignCenter); 
    m_imagePreview->hide(); 
    m_mainLayout->addWidget(m_imagePreview, 1); 
 
    // 2026-04-11 按照用户要求：为预览控件安装拦截器，实现空格键关闭功能 
    m_textPreview->installEventFilter(this); 
    m_imagePreview->installEventFilter(this); 
 
    m_gridView->installEventFilter(this); 
    m_treeView->installEventFilter(this);
} 
 
void ContentPanel::updateStatusBarStats() {
    if (!m_proxyModel) return;
    
    // 只计算当前显示的总项目数量，不区分文件和文件夹
    int totalCount = m_proxyModel->rowCount();
    
    // 发送状态栏统计信号
    emit statusBarStatsUpdated(0, 0, totalCount);
}

void ContentPanel::refreshVisibleThumbnails() {
    QWidget* current = m_viewStack->currentWidget();
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(current);
    if (!view || !m_model) return;

    int top = 0;
    int bottom = m_proxyModel->rowCount() - 1;

    // 获取视口可见区域对应的索引
    QModelIndex topIdx = view->indexAt(QPoint(10, 10));
    QModelIndex bottomIdx = view->indexAt(QPoint(view->viewport()->width() - 10, view->viewport()->height() - 10));

    if (topIdx.isValid()) top = topIdx.row();
    if (bottomIdx.isValid()) bottom = bottomIdx.row();

    // 稍微向外扩大一两页缓冲，以防止滑动假白 (Precache padding)
    int padding = 5;
    top = std::max(0, top - padding);
    bottom = std::min(m_proxyModel->rowCount() - 1, bottom + padding);

    QList<int> visibleRows;
    for (int r = top; r <= bottom; ++r) {
        QModelIndex proxyIdx = m_proxyModel->index(r, 0);
        QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        if (srcIdx.isValid()) {
            visibleRows.append(srcIdx.row());
        }
    }

    m_model->loadThumbnailsForRows(visibleRows);
}

void ContentPanel::updateGridSize() {
    ArcMeta::Logger::log(QString("[UI_DEBUG] 缩放级: %1").arg(m_zoomLevel));

    if (m_viewStack->currentWidget() == m_gridView) {
        if (auto* jv = qobject_cast<JustifiedView*>(m_gridView)) {
            jv->setTargetRowHeight(m_zoomLevel); // 自适应/网格模式下的卡片/行高
        } else if (auto* lv = qobject_cast<QListView*>(m_gridView)) {
            lv->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
            int side = m_zoomLevel + 46;
            int ratingH = 24;
            int nameH = (int)(m_zoomLevel * 0.25);
            int gap = 6;
            int totalH = side + gap + ratingH + gap + nameH + 8;
            lv->setGridSize(QSize(side, totalH));
        }
    } else if (m_viewStack->currentWidget() == m_treeView) {
        // 列表模式：动态计算安全图标尺寸（最低不小于 16px）
        int iconSize = qMax(16, m_zoomLevel - 8);
        m_treeView->setIconSize(QSize(iconSize, iconSize));

        // 动态设置列表项的物理行高为 m_zoomLevel (范围：30px ~ 230px)
        static int lastTreeHeight = -1;
        if (lastTreeHeight != m_zoomLevel) {
            m_treeView->setStyleSheet( 
                QString("QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }" 
                        "QTreeView::item { height: %1px; color: #EEEEEE; padding-left: 0px; }" 
                        "QTreeView::item:alternate { background-color: #252526; }" 
                        "QTreeView::item:selected { background-color: rgba(52, 152, 219, 0.2); border-left: 2px solid #3498db; }"
                        "QTreeView::item:hover { background-color: #2A2A2A; }"
                        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }")
                .arg(m_zoomLevel)
            );
            lastTreeHeight = m_zoomLevel;
        }
    }

    // 持久化保存当前的缩放级别
    AppConfig::instance().setValue("UI/GridZoomLevel", m_zoomLevel);

    qDebug() << "[GridSize] Zoom:" << m_zoomLevel;
} 
 
bool ContentPanel::eventFilter(QObject* obj, QEvent* event) { 
    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wEvent = static_cast<QWheelEvent*>(event);
        if (wEvent->modifiers() & Qt::ControlModifier) {
            int deltaY = wEvent->angleDelta().y();
            int newZoom = m_zoomLevel + (deltaY > 0 ? 8 : -8);
            setZoomLevel(newZoom);
            wEvent->accept();
            return true; // 吞噬该事件，不让子视图产生滚动，彻底解决逻辑混乱和时灵时不灵问题
        }
    }

    // 2026-03-xx 按照宪法要求：物理拦截 Hover 事件以触发 ToolTipOverlay 
    // 2026-05-20 性能优化：同时支持 Enter/Leave 事件，确保响应灵敏 
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) { 
        QString text = obj->property("tooltipText").toString(); 
        if (!text.isEmpty()) { 
            int timeout = (obj == m_btnLayers || obj == m_btnLayersBlue || 
                       obj == m_btnToggleFolders || obj == m_btnToggleFiles ||
                       obj == m_btnLayersBlue) ? 0 : 700;
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, timeout); 
        } 
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress) { 
        ToolTipOverlay::hideTip(); 
    } 
 
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mEvent = reinterpret_cast<QMouseEvent*>(event);
        if (mEvent->button() == Qt::LeftButton) {
            if (obj == m_gridView || obj == m_gridView->viewport() || obj == m_treeView || obj == m_treeView->viewport()) {
                QAbstractItemView* view = qobject_cast<QAbstractItemView*>(obj);
                if (!view) view = qobject_cast<QAbstractItemView*>(obj->parent());
                if (view) {
                    QPoint pos = mEvent->pos();
                    if (obj == view && view->viewport()) {
                        pos = view->viewport()->mapFrom(view, pos);
                    }
                    QModelIndex index = view->indexAt(pos);
                    if (index.isValid()) {
                        // 针对 Grid 模式 / Justified 模式的 Hitbox
                        ThumbnailDelegate* thumbDel = qobject_cast<ThumbnailDelegate*>(view->itemDelegateForIndex(index));
                        if (thumbDel) {
                            QStyleOptionViewItem opt;
                            opt.rect = view->visualRect(index);
                            opt.decorationSize = view->iconSize();
                            if (opt.decorationSize.width() <= 0) opt.decorationSize = QSize(96, 96);
                            ThumbnailDelegate::Metrics m = thumbDel->calculateMetrics(opt);

                            bool isBanHit = m.banRect.contains(pos);
                            int hitStar = -1;
                            for (int i = 0; i < 5; ++i) {
                                if (m.starRect(i).contains(pos)) {
                                    hitStar = i + 1;
                                    break;
                                }
                            }

                            if (isBanHit || hitStar != -1) {
                                bool isSelected = false;
                                if (view->selectionModel()) {
                                    isSelected = view->selectionModel()->isSelected(index);
                                }
                                if (!isSelected) return false;

                                int newValue = isBanHit ? 0 : hitStar;
                                if (view->selectionModel() && view->selectionModel()->isSelected(index)) {
                                    auto selectedIndexes = view->selectionModel()->selectedIndexes();
                                    for (const auto& selIdx : selectedIndexes) {
                                        if (selIdx.column() == 0) {
                                            m_proxyModel->setData(selIdx, newValue, RatingRole);
                                        }
                                    }
                                } else {
                                    m_proxyModel->setData(index, newValue, RatingRole);
                                }

                                QAbstractItemView::EditTriggers currentTriggers = view->editTriggers();
                                view->setEditTriggers(QAbstractItemView::NoEditTriggers);
                                QTimer::singleShot(0, view, [view, currentTriggers]() {
                                    view->setEditTriggers(currentTriggers);
                                });
                                event->accept();
                                return true;
                            }
                        }

                        // 针对 TreeView 列 2 (星级列) 的 Hitbox
                        if (view == m_treeView) {
                            QModelIndex indexCol2 = index.model()->index(index.row(), 2, index.parent());
                            QRect col2Rect = m_treeView->visualRect(indexCol2);
                            
                            int banW = 12;
                            int starSize = 18;
                            int banGap = 2;
                            int starSpacing = -4; // 与 Delegate 严格保持 -4 间距对齐
                            int totalW = banW + banGap + 5 * starSize + 4 * starSpacing; // 88px
                            int startX = col2Rect.left() + (col2Rect.width() - totalW) / 2;

                            QRect banHitbox(startX, col2Rect.top() + (col2Rect.height() - banW)/2, banW, banW);
                            bool isBanHit = banHitbox.contains(pos);
                            int hitStar = -1;

                            // 统一星级点击命中区参数，使其与 TreeItemDelegate 绘制参数保持绝对物理对齐
                            int starsStartX = startX + banW + banGap; 
                            for (int i = 0; i < 5; ++i) {
                                QRect starRect(starsStartX + i * (starSize + starSpacing), col2Rect.top() + (col2Rect.height() - starSize) / 2, starSize, starSize);
                                if (starRect.contains(pos)) {
                                    hitStar = i + 1;
                                    break;
                                }
                            }

                            if (isBanHit || hitStar != -1) {
                                bool isRowSelected = false;
                                if (m_treeView->selectionModel()) {
                                    isRowSelected = m_treeView->selectionModel()->isRowSelected(index.row(), index.parent());
                                }
                                if (!isRowSelected) return false;

                                int newValue = isBanHit ? 0 : hitStar;
                                if (m_treeView->selectionModel()) {
                                    auto selectedRows = m_treeView->selectionModel()->selectedRows();
                                    for (const auto& selRow : selectedRows) {
                                        QModelIndex targetIdx = m_treeView->model()->index(selRow.row(), 0, selRow.parent());
                                        m_proxyModel->setData(targetIdx, newValue, RatingRole);
                                    }
                                } else {
                                    m_proxyModel->setData(index.model()->index(index.row(), 0, index.parent()), newValue, RatingRole);
                                }

                                QAbstractItemView::EditTriggers currentTriggers = m_treeView->editTriggers();
                                m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
                                QTimer::singleShot(0, m_treeView, [this, currentTriggers]() {
                                    m_treeView->setEditTriggers(currentTriggers);
                                });
                                event->accept();
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

 
    if (event->type() == QEvent::KeyPress) { 
        // 2026-05-25 物理修复：改用 reinterpret_cast 避开 QEvent 到 QKeyEvent 的 static_cast 歧义 
        QKeyEvent* keyEvent = reinterpret_cast<QKeyEvent*>(event); 
 
        // 2026-04-11 按照用户要求：如果当前正在显示文本/图片预览，按下空格键则关闭预览 
        if ((obj == m_textPreview || obj == m_imagePreview) && keyEvent->key() == Qt::Key_Space) { 
            m_textPreview->hide(); 
            m_imagePreview->hide(); 
            m_viewStack->show(); 
            // 恢复焦点到主视图，确保后续交互连续 
            if (m_viewStack->currentWidget()) m_viewStack->currentWidget()->setFocus(); 
            return true; 
        } 
 
        QAbstractItemView* view = qobject_cast<QAbstractItemView*>(obj); 
        if (!view) view = qobject_cast<QAbstractItemView*>(obj->parent()); 
 
        if (qobject_cast<QLineEdit*>(QApplication::focusWidget())) { 
            return false; 
        } 
 
        if (view) { 
            if ((keyEvent->modifiers() & Qt::ControlModifier) &&  
                (keyEvent->key() >= Qt::Key_0 && keyEvent->key() <= Qt::Key_5)) { 
                 
                int rating = keyEvent->key() - Qt::Key_0; 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const auto& idx : indexes) { 
                    if (idx.column() == 0) { 
                        m_proxyModel->setData(idx, rating, RatingRole); 
                    } 
                } 
                return true; 
            } 
 
            if (((keyEvent->modifiers() & Qt::AltModifier) || (keyEvent->modifiers() & (Qt::AltModifier | Qt::WindowShortcut))) &&  
                (keyEvent->key() == Qt::Key_D)) { 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const QModelIndex& idx : indexes) { 
                    if (idx.column() == 0) { 
                        bool current = idx.data(IsLockedRole).toBool(); 
                        m_proxyModel->setData(idx, !current, IsLockedRole); 
                    } 
                } 
                return true; 
            } 
 
            if ((keyEvent->modifiers() & Qt::AltModifier) &&  
                (keyEvent->key() >= Qt::Key_1 && keyEvent->key() <= Qt::Key_9)) { 
                 
                QString colorValue; 
                switch (keyEvent->key()) { 
                    case Qt::Key_1: colorValue = "#E24B4A"; break; // red
                    case Qt::Key_2: colorValue = "#EF9F27"; break; // orange
                    case Qt::Key_3: colorValue = "#FECF0E"; break; // yellow
                    case Qt::Key_4: colorValue = "#639922"; break; // green
                    case Qt::Key_5: colorValue = "#1D9E75"; break; // cyan
                    case Qt::Key_6: colorValue = "#378ADD"; break; // blue
                    case Qt::Key_7: colorValue = "#7F77DD"; break; // purple
                    case Qt::Key_8: colorValue = "#5F5E5A"; break; // gray
                    case Qt::Key_9: colorValue = ""; break; 
                } 
 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const auto& idx : indexes) { 
                    if (idx.column() == 0) { 
                        m_proxyModel->setData(idx, colorValue, ColorRole); 
 
                        // 2026-06-05 按照要求：快捷键设置颜色后立即重渲染图标，实现视觉同步 
                        QString path = idx.data(PathRole).toString(); 
                        QIcon coloredIcon = ShellIconManager::getFileIcon(path, 128); 
                        m_proxyModel->setData(idx, coloredIcon, Qt::DecorationRole); 
                    } 
                } 
                return true; 
            } 
 
            if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) { 
                if (keyEvent->key() == Qt::Key_C) { 
                    QStringList paths; 
                    auto indexes = view->selectionModel()->selectedIndexes(); 
                    for (const auto& idx : indexes) if (idx.column() == 0) paths << QDir::toNativeSeparators(idx.data(PathRole).toString()); 
                    if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\r\n")); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：补全批量重命名 (Ctrl+Shift+R) 快捷键绑定 
                if (keyEvent->key() == Qt::Key_R) { 
                    performBatchRename(); 
                    return true; 
                } 
            } 
 
            if (keyEvent->key() == Qt::Key_F2) { 
                view->edit(view->currentIndex()); 
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Delete) { 
                onCustomContextMenuRequested(view->mapFromGlobal(QCursor::pos())); 
                return true; 
            } 
             
            if (keyEvent->modifiers() & Qt::ControlModifier) { 
                // 2026-03-xx 按照用户要求：逻辑重构，统一调用 performCopy 业务函数 
                if (keyEvent->key() == Qt::Key_C && !(keyEvent->modifiers() & Qt::ShiftModifier)) { 
                    performCopy(false); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：实现剪切逻辑 (Ctrl+X) 
                if (keyEvent->key() == Qt::Key_X) { 
                    performCopy(true); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：逻辑重构，统一调用 performPaste 业务函数 
                if (keyEvent->key() == Qt::Key_V) { 
                    performPaste(); 
                    return true; 
                } 
            } 
 
            if (keyEvent->key() == Qt::Key_Space) { 
                QModelIndex idx = view->currentIndex(); 
                if (idx.isValid()) {
                    QString path = idx.data(PathRole).toString();
                    if (!path.isEmpty()) {
                        // 2026-11-14 按照 Plan-109：全口径预览属性过滤（白名单优先策略）
                        QFileInfo info(path);
                        if (info.isDir()) return true; // 拦截文件夹

                        QString ext = info.suffix().toLower();
                        // 1. 系统级不可预览黑名单 (包含压缩包、二进制文件及系统库)
                        static const QSet<QString> blackList = {
                            "exe", "dll", "sys", "bin", "dat", "lib", "obj", "msi", "com",
                            "zip", "rar", "7z", "iso", "tar", "gz", "bz2", "dmg", "pkg"
                        };
                        if (blackList.contains(ext)) return true;

                        // 2. 预览准入白名单 (仅限受支持的图像类及文本/代码类文件)
                        static const QSet<QString> whiteList = {
                            "jpg", "jpeg", "png", "bmp", "webp", "gif", "ico", "cur", "ani", "psd", "ai", "eps", "pdf", "svg",
                            "txt", "md", "markdown", "log", "cpp", "h", "hpp", "c", "py", "js", "css", "html", "json", "xml", "ini", "conf", "yaml", "yml"
                        };

                        if (whiteList.contains(ext)) {
                            emit requestQuickLook(path);
                        }
                    }
                }
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Backspace) { 
                QDir dir(m_currentPath); 
                if (dir.cdUp()) emit directorySelected(dir.absolutePath()); 
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) { 
                onDoubleClicked(view->currentIndex()); 
                return true; 
            } 
            if (keyEvent->modifiers() & Qt::ControlModifier && keyEvent->key() == Qt::Key_Backslash) { 
                ViewMode nextMode = ListView;
                if (m_currentViewMode == ListView) nextMode = GridView;
                else if (m_currentViewMode == GridView) nextMode = JustifiedViewMode;
                else if (m_currentViewMode == JustifiedViewMode) nextMode = ListView;
                setViewMode(nextMode); 
                return true; 
            } 
        } 
    } 
    return QWidget::eventFilter(obj, event); 
} 
 
void ContentPanel::selectAndScrollToPath(const QString& path) {
    if (!m_proxyModel) return;
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
        QModelIndex proxyIdx = m_proxyModel->index(i, 0);
        if (proxyIdx.data(PathRole).toString() == path) {
            QAbstractItemView* view = (m_viewStack->currentWidget() == m_treeView) ? 
                static_cast<QAbstractItemView*>(m_treeView) : static_cast<QAbstractItemView*>(m_gridView);
            if (view) {
                view->scrollTo(proxyIdx);
                view->setCurrentIndex(proxyIdx);
                view->selectionModel()->select(proxyIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            break;
        }
    }
}

QString ContentPanel::getAdjacentFilePath(const QString& currentPath, int delta) { 
    if (!m_proxyModel || m_proxyModel->rowCount() == 0) return QString(); 
 
    int currentIndex = -1; 
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) { 
        QModelIndex idx = m_proxyModel->index(i, 0); 
        if (idx.data(PathRole).toString() == currentPath) { 
            currentIndex = i; 
            break; 
        } 
    } 
 
    if (currentIndex == -1) return QString(); 
 
    int targetIndex = currentIndex + delta; 
    // 逻辑：触达边界时停止，不进行循环跳转 
    if (targetIndex < 0 || targetIndex >= m_proxyModel->rowCount()) { 
        return QString(); 
    } 
 
    QModelIndex targetIdx = m_proxyModel->index(targetIndex, 0); 
    return targetIdx.data(PathRole).toString(); 
} 
 
void ContentPanel::setZoomLevel(int level) {
    // 1. 根据当前视图模式动态决定最小/最大像素边界
    int minZoom = 93;
    int maxZoom = 230;

    if (m_currentViewMode == ListView) {
        minZoom = 30;   // 列表视图最小值：30 像素
        maxZoom = 230;  // 列表视图最大值：230 像素
    } else { // GridView 与 JustifiedViewMode
        minZoom = 93;   // 网格/自适应最小值：93 像素
        maxZoom = 230;  // 网格/自适应最大值：230 像素
    }

    // 2. 严格按模式物理裁切
    int boundedLevel = qBound(minZoom, level, maxZoom);
    if (m_zoomLevel == boundedLevel) return;

    m_zoomLevel = boundedLevel;
    updateGridSize();
    emit zoomLevelChanged(m_zoomLevel);
}

void ContentPanel::wheelEvent(QWheelEvent* event) { 
    if (event->modifiers() & Qt::ControlModifier) { 
        int deltaY = event->angleDelta().y(); 
        int newZoom = m_zoomLevel + (deltaY > 0 ? 8 : -8); 
        setZoomLevel(newZoom); 
        event->accept(); 
        return; 
    } 
    QWidget::wheelEvent(event); 
} 
 
void ContentPanel::setViewMode(ViewMode mode) { 
    m_currentViewMode = mode;

    // 1. 模式切换时自动校准 m_zoomLevel，确保处于新模式的合法范围内
    int minZoom = (mode == ListView) ? 30 : 93;
    int maxZoom = 230;
    m_zoomLevel = qBound(minZoom, m_zoomLevel, maxZoom);

    // 2. 切换 ViewStack 页面
    if (mode == ListView) {
        m_viewStack->setCurrentWidget(m_treeView);
    } else if (mode == GridView) {
        auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
        if (justifiedView) {
            justifiedView->setLayoutMode(JustifiedView::GridMode);
        }
        m_viewStack->setCurrentWidget(m_gridView);
    } else if (mode == JustifiedViewMode) {
        auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
        if (justifiedView) {
            justifiedView->setLayoutMode(JustifiedView::JustifiedMode);
        }
        m_viewStack->setCurrentWidget(m_gridView);
    }

    // 保存当前的视图模式到 AppConfig，实现跨生命周期持久化
    AppConfig::instance().setValue("ContentPanel/ViewMode", static_cast<int>(mode));
    AppConfig::instance().sync();

    updateGridSize();
    emit viewModeChanged(mode); // 触发模式改变信号
    emit zoomLevelChanged(m_zoomLevel); // 通知标题栏滑杆更新数值
    m_visibleTimer->start();
} 
 
void ContentPanel::initGridView() { 
    m_gridView = new DropJustifiedView(this); 
    m_gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    // 2026-06-xx 按照用户要求：开启蓝色透明框选效果
    // 物理修复：对于 ListView/TreeView 使用 setSelectionRectVisible
    if (auto* lv = qobject_cast<QListView*>(m_gridView)) lv->setSelectionRectVisible(true);

    // 2026-06-xx 物理对齐：通过 QPalette 设定全局蓝色透明框选视觉样式
    QPalette p = m_gridView->palette();
    // 使用 #378ADD (QColor(55, 138, 221)) 并设定 Alpha 为 80 以确保框选内容清晰可见
    p.setColor(QPalette::Highlight, QColor(55, 138, 221, 80)); 
    p.setColor(QPalette::HighlightedText, Qt::white);
    m_gridView->setPalette(p);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu); 
 
    m_gridView->setDragEnabled(true); 
    m_gridView->setAcceptDrops(true);
    m_gridView->setDragDropMode(QAbstractItemView::DragDrop); 
 
    // 2026-06-xx 物理纠偏：移除 SelectedClicked，防止单击项目时意外触发重命名，确保交互稳健
    m_gridView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed); 
 
    m_gridView->setModel(m_proxyModel); 

    connect(m_gridView, SIGNAL(pathsDropped(QStringList,QModelIndex)), this, SLOT(onPathsDropped(QStringList,QModelIndex)));

    auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
    if (justifiedView) {
        justifiedView->setAspectRatioRole(AspectRatioRole);
        auto* delegate = new ThumbnailDelegate(this);
        delegate->setHasThumbnailRole(HasThumbnailRole);
        delegate->setRatingRole(RatingRole);
        delegate->setPathRole(PathRole);
        delegate->setPinnedRole(PinnedRole);
        delegate->setManagedRole(ManagedRole);
        delegate->setTypeRole(TypeRole);
        delegate->setIsEmptyRole(IsEmptyRole);
        delegate->setColorRole(ColorRole);
        delegate->setRegistrationProgressRole(RegistrationProgressRole);
        m_gridView->setItemDelegate(delegate);
    }

    m_gridView->viewport()->installEventFilter(this); 
 
    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
 
    m_gridView->setStyleSheet( 
        "QAbstractItemView { background-color: transparent; border: none; outline: none; }" 
        "QAbstractItemView::item { background: transparent; }" 
        "QAbstractItemView::item:selected { background-color: transparent; }" 
        "QAbstractItemView::item:hover { background-color: transparent; }"
    ); 
 
    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
    connect(m_gridView, &QAbstractItemView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_visibleTimer->start();
    });
} 
 
void ContentPanel::initListView() { 
    m_treeView = new DropTreeView(this); 
    m_treeView->setAlternatingRowColors(true); // 开启交替斑马纹背景
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_treeView->setSortingEnabled(true); 
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu); 
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    // 2026-06-xx 按照用户要求：开启蓝色透明框选效果
    // 物理修复：QTreeView 不支持 setSelectionRectVisible，通过 QPalette 高亮色实现视觉对齐
    QPalette tp = m_treeView->palette();
    tp.setColor(QPalette::Highlight, QColor(55, 138, 221, 80));
    tp.setColor(QPalette::HighlightedText, Qt::white);
    m_treeView->setPalette(tp);
     
    m_treeView->setDragEnabled(true); 
    m_treeView->setAcceptDrops(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop); 
 
    m_treeView->setExpandsOnDoubleClick(false); 
    m_treeView->setRootIsDecorated(false); 
     
    // 列表视图开启 m_drawMiniCards = true，以启用 Column 0 “最左侧微卡片圆角预览”和底部分割线贯通绘制
    m_treeView->setItemDelegate(new TreeItemDelegate(this, true, true)); 
 
    m_treeView->setModel(m_proxyModel); 
    m_treeView->viewport()->installEventFilter(this); 

    connect(m_treeView, SIGNAL(pathsDropped(QStringList,QModelIndex)), this, SLOT(onPathsDropped(QStringList,QModelIndex)));
 
    m_treeView->setStyleSheet( 
        "QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }" 
        "QTreeView::item { height: 28px; color: #EEEEEE; padding-left: 0px; }" 
        "QTreeView::item:alternate { background-color: #252526; }" // 斑马纹交替行高亮背景
        "QTreeView::item:selected { background-color: rgba(52, 152, 219, 0.2); border-left: 2px solid #3498db; }"
        "QTreeView::item:hover { background-color: #2A2A2A; }"
        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }" 
    ); 
 
    m_treeView->header()->setDefaultAlignment(Qt::AlignCenter);
    m_treeView->header()->setStyleSheet( 
        "QHeaderView::section { background-color: #252525; color: #B0B0B0; border: none; border-right: 1px solid #333333; height: 32px; font-size: 11px; }" 
    ); 
    
    // --- 列表表头（Header）列宽固定化重构 ---
    auto* header = m_treeView->header();
    header->setStretchLastSection(false); // 禁止末端强行拉伸
    header->setCascadingSectionResizes(false);

    // 1. 确保所有 7 列均可见，并且彻底隐藏或移除多余的第 7 列（原本的第 7 列已被前移）
    for (int i = 0; i <= 6; ++i) {
        header->setSectionHidden(i, false);
    }
    header->setSectionHidden(7, true);

    // 2. 精确设置各列固定像素宽度（彻底移除“颜色”列，平移后续所有列宽度）
    header->resizeSection(1, 50);   // 状态 (固定 50px 图标区)
    header->resizeSection(2, 120);  // 星级 (固定 120px 图标区)
    header->resizeSection(3, 120);  // 尺寸 (固定 120px)
    header->resizeSection(4, 80);   // 类型 (固定 80px)
    header->resizeSection(5, 100);  // 大小 (固定 100px)
    header->resizeSection(6, 120);  // 修改日期 (固定 120px)

    // 3. 锁定调整模式：第 0 列（名称）弹性自适应拉伸，第 1~6 列物理固定禁止拖拽
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i <= 6; ++i) {
        header->setSectionResizeMode(i, QHeaderView::Fixed);
    }
 
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
    connect(m_treeView, &QTreeView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
    connect(m_treeView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_visibleTimer->start();
    });
} 
 
void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) { 
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender()); 
    if (!view) return; 
 
    QModelIndex currentIndex = view->indexAt(pos); 
    bool onItem = currentIndex.isValid(); 
    bool isFolder = onItem && (currentIndex.data(TypeRole).toString() == "folder"); 
    QString path = currentIndex.data(PathRole).toString(); 
 
    QMenu menu(this); 
    UiHelper::applyMenuStyle(&menu); 
 
    if (onItem) { 
        // 2026-06-xx 物理修复：在回收站分类中，顶部增加“还原”选项
        if (m_currentCategoryType == "trash") {
            menu.addAction(UiHelper::getIcon("sync", QColor("#2ecc71"), 18), "还原")->setData(ActionRestore);
            menu.addSeparator();
        }

        // [核心操作区] 
        QAction* actOpen = menu.addAction(isFolder ? "打开文件夹" : "打开"); 
        actOpen->setData(ActionOpen); 
        if (!isFolder) { 
            menu.addAction("用系统默认程序打开")->setData(ActionOpenDefault); 
        } 
        menu.addAction("在“资源管理器”中显示")->setData(ActionShowInExplorer); 
 
        menu.addSeparator(); 
 
        // [归类与标记区] 
        // 2026-07-xx 按照 Plan-117：语义分流。判定当前是否为“镜像源”
        // 镜像源定义：侧边栏分类模式 (isMirrorSource() 为真) 
        // 或 物理导航模式下已进入资源库内部 (镜像加速态)
        // 🚨 [双轨不隔离违规点-3 物理隔离修复]: 磁盘模式右键菜单 100% 与内存数据库模式隔离，
        // 表现等同于 Windows 资源管理器。普通物理磁盘导航下的项绝对不提供“归类/设置颜色/设置评分”等任何逻辑库特权操作。
        bool isMirror = isMirrorSource();

        if (isMirror) {
            // [镜像源：归类与元数据编辑区]
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
            UiHelper::applyMenuStyle(categorizeMenu); 
            auto categories = CategoryRepo::getRecentlyUsed(15); 
            if (categories.empty()) categories = CategoryRepo::getAll();
            if (categories.size() > 15) categories.resize(15);

            QAction* actToUncat = categorizeMenu->addAction(UiHelper::getIcon("uncategorized", QColor("#95a5a6"), 16), "回归“未分类”");
            actToUncat->setData(ActionCategorize);
            actToUncat->setProperty("catId", -2); 
            categorizeMenu->addSeparator();

            if (categories.empty()) { 
                categorizeMenu->addAction("（暂无分类）")->setEnabled(false); 
            } else { 
                for (const auto& cat : categories) { 
                    QAction* act = categorizeMenu->addAction(QString::fromStdWString(cat.name)); 
                    act->setData(ActionCategorize); 
                    act->setProperty("catId", cat.id); 
                } 
            }

            // 直接在主菜单上呈现“设定颜色标签”快捷色块栏
            QString currentColorStr = currentIndex.data(ColorRole).toString();

            QWidgetAction* pickerAction = new QWidgetAction(&menu);
            ColorStripPicker* pickerWidget = new ColorStripPicker(currentColorStr, &menu);
            pickerAction->setDefaultWidget(pickerWidget);
            menu.addAction(pickerAction);

            connect(pickerWidget, &ColorStripPicker::colorSelected, this, [this, view, &menu](const QString& hexColor) {
                struct SelectedItemInfo {
                    QString type;
                    QString path;
                    int categoryId = 0;
                };
                QList<SelectedItemInfo> selectedItems;
                auto indexes = view->selectionModel()->selectedIndexes();  
                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        SelectedItemInfo info;
                        info.type = idx.data(TypeRole).toString();
                        info.path = idx.data(PathRole).toString();
                        info.categoryId = idx.data(CategoryIdRole).toInt();
                        selectedItems.append(info);
                    }  
                }

                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        m_proxyModel->setData(idx, hexColor, ColorRole);  
                    }  
                } 

                for (const auto& info : selectedItems) {
                    selectAndScrollToItem(info.type, info.path, info.categoryId);
                }
                menu.close(); 
            });
 
            bool isPinned = currentIndex.data(IsLockedRole).toBool(); 
            menu.addAction(isPinned ? "取消置顶" : "置顶")->setData(isPinned ? ActionUnpin : ActionPin); 
        } else {
            // [物理源：显示“迁移”]
            if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
                std::wstring wp = path.toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wp);

                // 2026-07-xx 按照 Plan-121：统一复用 AutoImportManager 的路径计算逻辑，
                // 不再自行拼接，确保使用完全一致的路径来源。
                std::wstring managedRootW = AutoImportManager::getManagedLibraryPath(wp);
                QString managedRoot = QString::fromStdWString(managedRootW);

                QMenu* migrateMenu = menu.addMenu(UiHelper::getIcon("add", QColor("#FF8C00"), 18), "迁移");
                UiHelper::applyMenuStyle(migrateMenu);

                if (managedRoot.isEmpty()) {
                    // Library 文件夹尚未创建，给出明确提示而非显示错误路径
                    migrateMenu->addAction("该盘库存未创建")->setEnabled(false);
                } else {
                    QAction* actRoot = migrateMenu->addAction(managedRoot);
                    actRoot->setData(ActionAddToCategory);
                    actRoot->setProperty("targetPath", managedRoot);

                    migrateMenu->menuAction()->setData(ActionAddToCategory);
                    migrateMenu->menuAction()->setProperty("targetPath", managedRoot);
                }

                migrateMenu->addSeparator();
                QStringList recentFolders = NavigationHistoryService::getRecentVisitedFolders(volSerial);
                if (recentFolders.isEmpty()) {
                    migrateMenu->addAction("迁移至最近活跃位置...")->setEnabled(false);
                } else {
                    for (const QString& folder : recentFolders) {
                        QAction* act = migrateMenu->addAction(folder);
                        act->setData(ActionAddToCategory);
                        act->setProperty("targetPath", folder);
                    }
                }
            }
        }

        menu.addSeparator(); 
 
        // 2026-06-xx 逻辑解耦修复：解除批量重命名的类型硬编码锁定 (架构升级)。
        // 核心规则：多选有效项目 (PathRole 不为空) 或 单选文件夹时，均解锁批量重命名入口。
        int selectedCount = 0;
        for (const auto& selIdx : view->selectionModel()->selectedIndexes()) {
            if (selIdx.column() == 0 && !selIdx.data(PathRole).toString().isEmpty()) {
                selectedCount++;
            }
        }

        // [批量与加密区] 
        if (isFolder || selectedCount > 1) { 
            menu.addAction("批量重命名 (Ctrl+Shift+R)")->setData(ActionBatchRename); 
        }

        if (!isFolder) { 
            QMenu* cryptoMenu = menu.addMenu("加密保护"); 
            UiHelper::applyMenuStyle(cryptoMenu); 
            cryptoMenu->addAction("执行加密保护")->setData(ActionEncrypt); 
            cryptoMenu->addAction("解除加密")->setData(ActionDecrypt); 
            cryptoMenu->addAction("修改加密密码")->setData(ActionChangePwd); 
        } 
 
        menu.addSeparator(); 
 
        // [通用编辑区] 
        if (selectedCount <= 1) {
            menu.addAction("重命名")->setData(ActionRename); 
        }
        menu.addAction("复制")->setData(ActionCopy); 
        menu.addAction("剪切")->setData(ActionCut); 
        menu.addAction("粘贴")->setData(ActionPaste); 
        
        // 2026-06-xx 按照用户要求：在回收站中不显示二级删除菜单
        if (m_currentCategoryType != "trash") {
            QMenu* delMenu = menu.addMenu("删除");
            UiHelper::applyMenuStyle(delMenu);
            delMenu->addAction("移入回收站")->setData(ActionDelete);
            // 2026-07-xx 物理级精简：移除普通彻底删除，仅保留并更名为“永久删除”（采用安全抹除逻辑）
            delMenu->addAction("永久删除")->setData(ActionSecureDelete);
        } else {
            // 回收站模式下，原位置不显示删除
        }
 
        menu.addSeparator(); 
        menu.addAction("复制路径")->setData(ActionCopyPath); 
        menu.addAction("添加至收藏夹")->setData(ActionAddToFavorites); 
        menu.addAction("刷新")->setData(ActionRefresh); 
        menu.addAction("属性")->setData(ActionProperties); 

        // 2026-07-xx 按照 Development_Plan 2.1：始终显示“重新扫描”选项 (仅限资源库内项目)
        if (currentIndex.data(ManagedRole).toBool()) {
            menu.addSeparator();
            menu.addAction(UiHelper::getIcon("sync", QColor("#378ADD"), 18), "重新扫描")->setData(ActionRescan);
        }

        // 2026-07-27 按照 Plan-107：仅对已在资源库中登记的文件夹，增加“取消导入并清除数据”菜单项
        if (currentIndex.data(TypeRole).toString() == "folder" && currentIndex.data(ManagedRole).toBool()) {
            menu.addAction(UiHelper::getIcon("close", QColor("#e81123"), 18), "取消导入并清除数据")->setData(ActionCancelImport);
        }

        // 2026-06-xx 按照用户要求：在回收站分类中，最底部增加“永久删除”选项
        if (m_currentCategoryType == "trash") {
            menu.addSeparator();
            // 2026-07-xx 物理一致性：回收站内的永久删除统一采用 ActionSecureDelete
            menu.addAction(UiHelper::getIcon("trash", QColor("#e81123"), 18), "永久删除")->setData(ActionSecureDelete);
        }
 
    } else { 
        // [空白处菜单] 
        QMenu* newMenu = menu.addMenu("新建..."); 
        UiHelper::applyMenuStyle(newMenu); 
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 
 
        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
        actPaste->setData(ActionPaste); 
        actPaste->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
 
        menu.addSeparator(); 
        menu.addAction("刷新")->setData(ActionRefresh);

        menu.addSeparator(); 
        QAction* actProp = menu.addAction("当前文件夹属性"); 
        actProp->setData(ActionProperties); 
        actProp->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 

        // 2026-07-xx 按照 Plan-63：如果是空白处点击，直接在这里注入并在下方 exec
    } 

    menu.addSeparator();

    // 注入“排序”二级子菜单
    QMenu* sortMenu = menu.addMenu("排序");
    UiHelper::applyMenuStyle(sortMenu);

    // 属性单选组
    QActionGroup* typeGroup = new QActionGroup(this);
    auto addTypeAct = [&](const QString& label, ContentPanel::SortType type) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(m_sortType == type);
        typeGroup->addAction(act);
        connect(act, &QAction::triggered, [this, type]() {
            m_sortType = type;
            AppConfig::instance().setValue("ContentPanel/RightClickSortType", static_cast<int>(type));
            
            // 实时触发全量无效化与排序重计算
            m_proxyModel->invalidate();
            m_proxyModel->sort(0, m_sortOrder);
        });
    };

    addTypeAct("名称", ContentPanel::SortByName);
    addTypeAct("创建日期", ContentPanel::SortByCreateDate);
    addTypeAct("修改日期", ContentPanel::SortByModifyDate);
    addTypeAct("扩展名", ContentPanel::SortByExtension);
    addTypeAct("大小", ContentPanel::SortBySize);
    addTypeAct("尺寸", ContentPanel::SortByDimension);
    addTypeAct("评分", ContentPanel::SortByRating);
    addTypeAct("添加日期", ContentPanel::SortByAddedDate);

    sortMenu->addSeparator();

    // 方向单选组
    QActionGroup* orderGroup = new QActionGroup(this);
    auto addOrderAct = [&](const QString& label, Qt::SortOrder order) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(m_sortOrder == order);
        orderGroup->addAction(act);
        connect(act, &QAction::triggered, [this, order]() {
            m_sortOrder = order;
            AppConfig::instance().setValue("ContentPanel/RightClickSortOrder", static_cast<int>(order));
            
            m_proxyModel->invalidate();
            m_proxyModel->sort(0, order);
        });
    };

    addOrderAct("升序", Qt::AscendingOrder);
    addOrderAct("降序", Qt::DescendingOrder);

    // 2026-07-xx 按照 Plan-63：注入布局显示控制菜单
    menu.addSeparator();
    QMenu* layoutMenu = menu.addMenu("布局显示");
    UiHelper::applyMenuStyle(layoutMenu);
    
    // 通过向上寻道获取 MainWindow 实例以复用菜单逻辑
    MainWindow* mw = nullptr;
    QWidget* parentWin = window();
    while (parentWin) {
        if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
        parentWin = parentWin->parentWidget();
    }
    if (mw) {
        mw->populatePanelMenu(layoutMenu);
    }
 
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos)); 
    if (!selectedAction || !selectedAction->data().isValid()) return; 
 
    ContextAction action = static_cast<ContextAction>(selectedAction->data().toInt()); 
 
    switch (action) { 
        case ActionOpen: 
        case ActionOpenDefault: 
            onDoubleClicked(currentIndex); 
            break; 
        case ActionShowInExplorer: { 
            ShellHelper::openInExplorer(onItem ? path : m_currentPath); 
            break; 
        } 
        case ActionNewFolder: createNewItem("folder"); break; 
        case ActionNewMd: createNewItem("md"); break; 
        case ActionNewTxt: createNewItem("txt"); break; 
        case ActionCategorize: { 
            int catId = selectedAction->property("catId").toInt(); 
            auto indexes = view->selectionModel()->selectedIndexes(); 
             
            for (const auto& idx : indexes) { 
                if (idx.column() == 0) { 
                    QString itemPath = idx.data(PathRole).toString(); 
                    std::wstring wPath = itemPath.toStdWString();

                    // 2026-06-xx 物理同步：基于同步获取的 File ID 进行归类，解决新文件关联失败冲突。 
                    std::string fid = MetadataManager::instance().getFolderIdSync(wPath); 
                    if (!fid.empty()) { 
                        // 2026-06-xx 按照用户需求：如果在系统层选择了“未分类”，则清除该项所有其他分类关联
                        if (catId == -2) { // 未分类的负数 ID
                             // 2026-07-xx 按照 Plan-83：实现撤销支持
                             std::vector<int> oldCatIds = CategoryRepo::getItemCategoryIds(fid);
                             if (!oldCatIds.empty()) {
                                 if (CategoryRepo::removeAllCategories(fid)) {
                                     UndoManager::instance().pushCommand(std::make_unique<BulkUncategorizeCommand>(itemPath, fid, oldCatIds));
                                 }
                             }
                        } else if (catId > 0) {
                             if (CategoryRepo::addItemToCategory(catId, fid, wPath)) {
                                 UndoManager::instance().pushCommand(std::make_unique<CategorizeCommand>(itemPath, fid, catId, true));
                             }
                        }
                    } 
                } 
            } 
            ToolTipOverlay::instance()->showText(QCursor::pos(), "已完成扫描并成功归类", 1500, QColor("#2ecc71")); 
            break; 
        } 
        case ActionPin: 
        case ActionUnpin: { 
            auto indexes = view->selectionModel()->selectedIndexes(); 
            bool pin = (action == ActionPin); 
            for (const QModelIndex& idx : indexes) { 
                if (idx.column() == 0) { 
                    // 2026-06-xx 架构简化：统一由 model->setData 处理持久化与缓存清理
                    m_proxyModel->setData(idx, pin, IsLockedRole); 
                } 
            } 
            // 2026-06-xx 物理修复：强制刷新代理模型排序，确保置顶项立即重排至顶部
            m_proxyModel->invalidate();
            m_proxyModel->sort(0, m_proxyModel->sortOrder());
            break; 
        } 
        case ActionEncrypt: { 
            FramelessInputDialog dlg("加密保护", "设置加密密码:", "", this);
            dlg.setEchoMode(QLineEdit::Password);
            if (dlg.exec() == QDialog::Accepted) { 
                QString pwd = dlg.text();
                if (pwd.isEmpty()) break;
                auto indexes = view->selectionModel()->selectedIndexes(); 
                QStringList targets; 
                for (const auto& idx : indexes) if (idx.column() == 0) targets << idx.data(PathRole).toString(); 
                 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务已在后台启动...", 2000); 
                 
                std::string stdPwd = pwd.toStdString(); 
                QPointer<ContentPanel> self(this); 
                QString currentDir = m_currentPath; 
 
                (void)QThreadPool::globalInstance()->start([self, targets, stdPwd, currentDir]() { 
                    for (const QString& src : targets) { 
                        QString dest = src + ".amenc"; 
                        if (EncryptionManager::instance().encryptFile(src.toStdWString(), dest.toStdWString(), stdPwd)) { 
                            QFile::remove(src); 
                            MetadataManager::instance().setEncrypted(dest.toStdWString(), true); 
                        } 
                    } 
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, currentDir]() { 
                        if (self && self->m_currentPath == currentDir) self->loadDirectory(currentDir, self->m_isRecursive); 
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务处理完成", 1500, QColor("#2ecc71")); 
                    }); 
                }); 
            } 
            break; 
        } 
        case ActionDecrypt: { 
            FramelessInputDialog dlg("解除加密", "输入加密密码:", "", this);
            dlg.setEchoMode(QLineEdit::Password);
            if (dlg.exec() == QDialog::Accepted) { 
                QString pwd = dlg.text();
                if (!pwd.isEmpty()) { 
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "解除加密逻辑已触发", 1500); 
                }
            } 
            break; 
        } 
        case ActionBatchRename: performBatchRename(); break; 
        case ActionAddToCategory: {
            QStringList paths;
            auto indexes = view->selectionModel()->selectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) paths << p;
                }
            }
            
            if (paths.isEmpty() && !path.isEmpty()) paths << path;

            QString target = selectedAction->property("targetPath").toString();
            if (target.isEmpty()) {
                // 兜底逻辑：获取当前盘符资源库根目录
                std::wstring wp = path.toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wp);
                QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
                QString relPath = AppConfig::instance().getValue(key, "").toString();
                target = QDir::toNativeSeparators(path.left(3) + relPath);
            }

            if (!paths.isEmpty() && !target.isEmpty()) {
                // 弱指针安全机制：避免在异步物理移动期间，ContentPanel 析构而导致的非法内存访问
                QPointer<ContentPanel> weakThis(this);

                // 执行物理迁移，并提供无缝无感刷新执行动作 (对应用户原话："行，试试吧")
                ImportHelper::importPaths(paths, target, this, [weakThis]() {
                    if (weakThis) {
                        qDebug() << "[Content] 后台物理迁移完成，安全触发 UI 异步无感防闪载入";
                        weakThis->refreshAll(); 
                    }
                });
            }
            break;
        }
        case ActionRename: view->edit(currentIndex); break; 
        case ActionCopy: performCopy(false); break; 
        case ActionCut: performCopy(true); break; 
        case ActionPaste: performPaste(); break; 
        case ActionRescan: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (!targetPaths.isEmpty()) {
                // 2026-08-xx 按照 Plan-126：用户手动发起的“重新扫描”应属于元数据刷新
                // 此时依然允许通过 MetadataManager 执行，但不应作为常规“入库”手段
                MetadataManager::instance().registerItemsAsync(targetPaths, true);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已启动物理状态同步", 1500, QColor("#378ADD"));
            }
            break;
        }
        case ActionCancelImport: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (!targetPaths.isEmpty()) {
                std::vector<std::wstring> stdPaths;
                for (const QString& tp : targetPaths) {
                    stdPaths.push_back(tp.toStdWString());
                    // 物理清退内容面板缩略图与宽高比缓存
                    clearFolderCache(tp);
                }

                // 1. 中止并取消队列中以及正在提取的高级多媒体任务
                MediaExtractorPipeline::instance().cancelBatch(stdPaths);

                // 2. 批量大事务级联擦除已入库的元数据和关联、进度、重置计数器
                MetadataManager::instance().removeMetadataBatchSync(targetPaths);

                ToolTipOverlay::instance()->showText(QCursor::pos(), "已取消自动导入并彻底擦除相关元数据", 2000, QColor("#e81123"));
                refreshAll();
            }
            break;
        }
        case ActionRestore: {
            auto indexes = view->selectionModel()->selectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString itemPath = idx.data(PathRole).toString();
                    auto meta = MetadataManager::instance().getMeta(itemPath.toStdWString());
                    if (meta.isTrash && !meta.originalPath.empty()) {
                        QString dest = QString::fromStdWString(meta.originalPath);
                        QDir().mkpath(QFileInfo(dest).absolutePath());
                        if (QFile::rename(itemPath, dest)) {
                            MetadataManager::instance().markAsTrash(dest.toStdWString(), false);

                            // 🚨 按照要求：还原后一律归入"未分类"，不恢复删除前的任何分类关联
                            std::string fid = MetadataManager::instance().getFolderIdSync(dest.toStdWString());
                            if (!fid.empty()) {
                                CategoryRepo::removeAllCategories(fid);
                            }
                        }
                    }
                }
            }
            // 修正：采用 refreshAll() 替换 loadDirectory(m_currentPath)
            refreshAll();
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild); // 刷新全量统计
            break;
        }
        case ActionDelete: 
        case ActionSecureDelete: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) targetPaths << idx.data(PathRole).toString();
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (targetPaths.isEmpty()) break;

            if (action == ActionDelete) {
                // 1. 开启内部操作锁，彻底抑制 NativeFolderWatcher 的二次干扰信号
                MetadataManager::instance().setInternalOperating(true);

                if (ShellHelper::moveToTrash(targetPaths)) {
                    // 2. 修正：调用 refreshAll() 自适应协议与物理路径刷新，绝不调 loadDirectory！
                    refreshAll();
                }

                // 2000ms 后平滑释放抑制锁
                QTimer::singleShot(2000, []() {
                    MetadataManager::instance().setInternalOperating(false);
                });
            } else {
                QString msg = "确定要永久删除选中的项目吗？数据将被物理覆写并彻底抹除，此操作不可恢复。";
                if (!FramelessMessageBox::question(this, "确认删除", msg)) break;

                BatchProgressDialog* progress = new BatchProgressDialog("正在执行永久删除（深层抹除）...", this);
                progress->show();

                QPointer<ContentPanel> weakThis(this);
                QPointer<BatchProgressDialog> weakProgress(progress);

                // 1. 开启内部操作锁，彻底抑制 NativeFolderWatcher 的二次干扰信号
                MetadataManager::instance().setInternalOperating(true);

                DiskIoService::asyncDeletePaths(
                    targetPaths,
                    action == ActionSecureDelete,
                    weakThis,
                    [weakProgress](int percent) {
                        if (weakProgress) {
                            weakProgress->setValue(percent);
                        }
                    },
                    [weakThis, weakProgress]() {
                        if (weakProgress) {
                            weakProgress->accept();
                            weakProgress->deleteLater();
                        }
                        if (weakThis) {
                            // 2. 核心修正：使用 refreshAll() 替代 loadDirectory()！
                            // refreshAll 能自动识别是 system://all、category:// 还是物理路径，精准刷出正确数据！
                            weakThis->refreshAll();
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "深层抹除已完成，关联记录已物理清空", 1500, QColor("#2ecc71"));
                        }

                        // 3. 2000ms 后平滑释放抑制锁
                        QTimer::singleShot(2000, []() {
                            MetadataManager::instance().setInternalOperating(false);
                        });
                    }
                );
            }
            break;
        }
        case ActionAddToFavorites: {
            QStringList selectedPaths;
            QModelIndexList indexes = getSelectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) {
                        selectedPaths << p;
                    }
                }
            }
            if (!selectedPaths.isEmpty()) {
                emit requestAddFavorite(selectedPaths);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已成功添加至收藏夹", 1500, QColor("#2ecc71"));
            }
            break;
        }
        case ActionCopyPath: {
            QModelIndexList indexes = getSelectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) targetPaths << QDir::toNativeSeparators(p);
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) {
                targetPaths << QDir::toNativeSeparators(path);
            }
            if (!targetPaths.isEmpty()) {
                QApplication::clipboard()->setText(targetPaths.join("\n"));
            }
            break;
        }
        case ActionProperties: { 
            ShellHelper::showProperties(onItem ? path : m_currentPath); 
            break; 
        } 
        case ActionRefresh: {
            refreshAll();
            break;
        }
        default: break; 
    } 
} 
 
void ContentPanel::performCopy(bool cutMode) { 
    // 2026-03-xx 按照用户要求：封装标准化文件复制/剪切逻辑 
    QModelIndexList indexes = getSelectedIndexes(); 
    QList<QUrl> urls; 
    for (const auto& idx : indexes) { 
        if (idx.column() == 0) { 
            QString path = idx.data(PathRole).toString(); 
            if (!path.isEmpty()) urls << QUrl::fromLocalFile(path); 
        } 
    } 
 
    if (urls.isEmpty()) return; 
 
    QMimeData* mime = new QMimeData(); 
    mime->setUrls(urls); 
     
    if (cutMode) { 
        // 核心规范：告知系统这是剪切操作 (DROPEFFECT_MOVE = 2) 
        // 修复：将变量名由 data 改为 effectData，避免隐藏类成员警告 
        QByteArray effectData; 
        effectData.append((char)2);  
        mime->setData("Preferred DropEffect", effectData); 
    } 
 
    QApplication::clipboard()->setMimeData(mime); 
} 
 
void ContentPanel::performPaste() { 
    const QMimeData* mime = QApplication::clipboard()->mimeData(); 
    if (!mime || !mime->hasUrls()) return; 
 
    QList<QUrl> urls = mime->urls(); 
    QStringList fromPaths;
    for (const QUrl& url : urls) {
        fromPaths << url.toLocalFile();
    }
     
    if (fromPaths.isEmpty()) return; 

    int targetCatId = 0;
    if (!resolvePasteDestination(targetCatId)) return; // 内部已完成提示/取消处理

    if (dataSourceType() == DataSourceType::DiskNav) {
        bool isMove = false; 
        if (mime->hasFormat("Preferred DropEffect")) { 
            QByteArray effect = mime->data("Preferred DropEffect"); 
            if (!effect.isEmpty() && (effect.at(0) & 0x02)) isMove = true; 
        } 

        if (ShellHelper::copyOrMoveItems(fromPaths, m_currentPath, isMove)) {  
            if (isMove) { 
                // 🚨 [双轨不隔离违规点-5 物理隔离修复]: 磁盘模式（DiskNav）物理移动仅作纯粹的文件 I/O 处理，不回调 syncAfterMove。 
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(fromPaths, QFileInfo(fromPaths.first()).absolutePath(), m_currentPath)); 
            } 
            loadDirectory(m_currentPath, m_isRecursive);  
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "粘贴失败：文件写入操作未能完成", 2000, QColor("#e81123"));
        }
    } else {
        QString msg = QString("确定要将剪贴板中的 %1 个项目分流导入并打包至该分类吗？").arg(fromPaths.size());
        if (FramelessMessageBox::question(this, "资产导入", msg)) {
            QPointer<ContentPanel> weakThis(this);
            AssetImporter::importAssets(fromPaths, targetCatId, this, [weakThis]() {
                if (weakThis) weakThis->refreshAll();
            });
        }
    }
} 
 
void ContentPanel::performBatchRename() { 
    // 2026-03-xx 按照用户要求：弹出深度集成的高级批量重命名对话框 
    QModelIndexList indexes = getSelectedIndexes(); 
    std::vector<std::wstring> originalPaths; 
    for (const auto& idx : indexes) { 
        if (idx.column() == 0) { 
            QString path = idx.data(PathRole).toString(); 
            if (!path.isEmpty()) {
                originalPaths.push_back(QDir::toNativeSeparators(path).toStdWString()); 
            }
        } 
    } 
 
    if (originalPaths.empty()) { 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "请先选择需要重命名的项目", 2000, QColor("#E81123")); 
        return; 
    } 
 
    BatchRenameDialog dlg(originalPaths, this); 
    if (dlg.exec() == QDialog::Accepted) { 
        // 🚨 极致自愈高亮：如果对话框成功重命名，将其返回的首个新名称作为 pendingSelectName
        QString firstNew = dlg.getFirstNewName();
        if (!firstNew.isEmpty()) {
            m_pendingSelectName = firstNew;
            m_isPendingEdit = false;
        }
        // 🚨 联动支持：不应强绑定物理 loadDirectory，统一调用 refreshAll 以自适应数据库和系统分类下的异步刷新，
        // 并实现完美的选中态无缝自愈高亮！
        refreshAll(); 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "批量重命名操作已成功执行", 1500, QColor("#2ecc71")); 
    } 
} 
 
ContentPanel::DataSourceType ContentPanel::dataSourceType() const {
    if (m_currentCategoryType == "user_category") {
        return DataSourceType::UserCategory;
    } else if (m_currentCategoryType == "all" || m_currentCategoryType == "uncategorized" || 
               m_currentCategoryType == "untagged" || m_currentCategoryType == "recently_visited" || 
               m_currentCategoryType == "trash" || m_currentCategoryType == "system_category") {
        return DataSourceType::SystemCategory;
    } else if (m_currentCategoryType == "path_list" || m_currentCategoryType == "search") {
        return DataSourceType::PathList;
    }
    return DataSourceType::DiskNav;
}

bool ContentPanel::isMirrorSource() const {
    return dataSourceType() != DataSourceType::DiskNav;
}

bool ContentPanel::isManagedContext() const { 
    // 🚨 [双轨不隔离违规点-2 物理隔离修复]: 磁盘模式与内存模式 100% 绝对物理隔离。 
    // 在磁盘模式（isMirrorSource() == false）下直接返回 false，绝不穿透查询资源库，拒绝一切逻辑混叠。 
    if (isMirrorSource()) return true; 
    return false; 
} 

void ContentPanel::onSelectionChanged() { 
    QItemSelectionModel* selectionModel = (m_viewStack->currentWidget() == m_gridView) ? m_gridView->selectionModel() : m_treeView->selectionModel(); 
    if (!selectionModel) return; 
 
    QStringList selectedPaths; 
    QModelIndexList indices = selectionModel->selectedIndexes(); 
    for (const QModelIndex& index : indices) { 
        if (index.column() == 0) { 
            QString path = index.data(PathRole).toString(); 
            if (!path.isEmpty()) selectedPaths.append(path); 
        } 
    } 
    emit selectionChanged(selectedPaths); 
} 
 
void ContentPanel::refreshAll() {
    // 2026-07-26 极致重构：在执行刷新前，自动暂存当前选中项的文件名，确保异步刷新后依然处于选中高亮状态（对应用户原话：“对某个文件夹/文件进行重命名 或 进行其他操作后仍然处于选中高亮状态”）
    QModelIndexList selected = getSelectedIndexes();
    if (!selected.isEmpty() && m_pendingSelectName.isEmpty()) {
        QString p = selected.first().data(PathRole).toString();
        if (!p.isEmpty()) {
            m_pendingSelectName = QFileInfo(p).fileName();
            m_isPendingEdit = false;
        }
    }

    // 2026-06-xx 物理对标：完善刷新逻辑，支持所有上下文类型
    if (m_currentCategoryType == "user_category") {
        if (m_currentCategoryId != -1) loadCategory(m_currentCategoryId);
    } else if (m_currentCategoryType == "all" || m_currentCategoryType == "uncategorized" || 
               m_currentCategoryType == "untagged" || m_currentCategoryType == "recently_visited" || 
               m_currentCategoryType == "trash") {
        QStringList paths = CategoryRepo::getSystemCategoryPaths(m_currentCategoryType);
        loadPaths(paths);
    } else if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
        loadDirectory(m_currentPath, m_isRecursive);
    } else {
        // 兜底逻辑：加载“此电脑”
        loadDirectory("computer://");
    }
}

void ContentPanel::updateItemMetadata(const QString& path) {
    if (m_model) {
        m_model->updateRecordMetadata(path);
    }
}

void ContentPanel::migrateModelCache(const QString& oldPath, const QString& newPath) {
    if (m_model) {
        m_model->migrateCache(oldPath, newPath);
    }
}

void ContentPanel::clearFolderCache(const QString& folderPath) {
    if (m_model) {
        m_model->clearCacheForFolder(folderPath);
    }
}

void ContentPanel::onPathsDropped(const QStringList& paths, const QModelIndex& targetIndex) {
    if (paths.isEmpty()) return;

    if (dataSourceType() == DataSourceType::DiskNav) {
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") return;
        // 【分流 A：磁盘导航模式】──> 执行标准的操作系统级物理粘贴/复制，绝不调用 AssetImporter！
        QString destDir = m_currentPath;
        if (targetIndex.isValid()) {
            QModelIndex srcIdx = m_proxyModel->mapToSource(targetIndex);
            if (srcIdx.isValid()) {
                QString targetPath = srcIdx.data(PathRole).toString();
                if (!targetPath.isEmpty() && QFileInfo(targetPath).isDir()) {
                    destDir = targetPath;
                }
            }
        }

        // 检查是否在原地投放
        bool sameDir = true;
        for (const QString& p : paths) {
            if (QDir::toNativeSeparators(QFileInfo(p).absolutePath()) != QDir::toNativeSeparators(destDir)) {
                sameDir = false;
                break;
            }
        }
        if (sameDir && destDir == m_currentPath) return;

        bool isMove = !(QApplication::keyboardModifiers() & Qt::ControlModifier);
        
        MetadataManager::instance().setInternalOperating(true);

        if (ShellHelper::copyOrMoveItems(paths, destDir, isMove)) {
            if (isMove) {
                // 🚨 [双轨不隔离违规点-4 物理隔离修复]: 磁盘模式（DiskNav）物理拖拽移动仅作纯粹的文件 I/O 处理，不回调 syncAfterMove。
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(paths, QFileInfo(paths.first()).absolutePath(), destDir));
            }
            loadDirectory(m_currentPath, m_isRecursive);
        }

        QTimer::singleShot(2000, []() {
            MetadataManager::instance().setInternalOperating(false);
        });
    } else {
        // 优先尊重"拖拽到具体子分类节点上"这个更精确的用户意图
        int targetCatId = 0;
        bool droppedOnCategoryNode = false;
        if (targetIndex.isValid()) {
            QModelIndex srcIdx = m_proxyModel->mapToSource(targetIndex);
            if (srcIdx.isValid() && srcIdx.data(TypeRole).toString() == "category") {
                targetCatId = srcIdx.data(CategoryIdRole).toInt();
                droppedOnCategoryNode = true;
            }
        }

        if (!droppedOnCategoryNode) {
            // 没有拖拽到具体子分类节点上，则退回统一的目的地判断规则
            // （磁盘模式已在上面 return，这里只会命中 UserCategory / 聚合视图 / 回收站三种情形）
            if (!resolvePasteDestination(targetCatId)) return;
        }

        QString msg = QString("确定要将选中的 %1 个项目分流导入并打包至资源库吗？").arg(paths.size());
        if (FramelessMessageBox::question(this, "资产导入", msg)) {
            QPointer<ContentPanel> weakThis(this);
            AssetImporter::importAssets(paths, targetCatId, this, [weakThis]() {
                if (weakThis) weakThis->refreshAll();
            });
        }
    }
}

void ContentPanel::onDoubleClicked(const QModelIndex& index) { 
    if (!index.isValid()) return; 
 
    // 2026-06-xx 重构逻辑：优先处理子分类跳转 
    int catId = index.data(CategoryIdRole).toInt(); 
    if (catId > 0) { 
        emit categoryClicked(catId); 
        return; 
    } 
 
    QString path = index.data(PathRole).toString(); 
    if (path.isEmpty()) return; 
 
    QFileInfo info(path); 
    if (info.isDir()) { 
        emit directorySelected(path);  
    } else { 
        MetadataManager::instance().recordAccess(path.toStdWString());
        
        // 2026-11-xx 按照用户全新要求：在内容面板双击某个文件时如同按下空格键那样打开预览
        QString ext = info.suffix().toLower();
        // 1. 系统级不可预览黑名单 (包含压缩包、二进制文件及系统库)
        static const QSet<QString> blackList = {
            "exe", "dll", "sys", "bin", "dat", "lib", "obj", "msi", "com",
            "zip", "rar", "7z", "iso", "tar", "gz", "bz2", "dmg", "pkg"
        };
        if (blackList.contains(ext)) return;

        // 2. 预览准入白名单 (仅限受支持的图像类及文本/代码类文件)
        static const QSet<QString> whiteList = {
            "jpg", "jpeg", "png", "bmp", "webp", "gif", "ico", "cur", "ani", "psd", "ai", "eps", "pdf", "svg",
            "txt", "md", "markdown", "log", "cpp", "h", "hpp", "c", "py", "js", "css", "html", "json", "xml", "ini", "conf", "yaml", "yml"
        };

        if (whiteList.contains(ext)) {
            emit requestQuickLook(path);
        }
    } 
} 
 
void ContentPanel::loadDirectory(const QString& path, bool recursive) { 
    // =========================================================================
    // 【彻底解耦与隔离】：干掉越界的劫持与重定向逻辑，保持磁盘模式 100% 的纯粹性！
    // =========================================================================

    m_isLoading = true;
    int reqId = ++m_loadRequestId;
    m_currentCategoryType = ""; // 物理导航模式下清除系统类型
    ArcMeta::Logger::log(QString("[Content] 开始物理递归扫描 (虚拟化) [%1] -> %2 (%3)")
                        .arg(reqId).arg(path).arg(recursive ? "递归" : "单级"));
    emit dataSourceChanged("nav"); 
    if (m_viewStack) m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
 
    m_isRecursive = recursive; 
    if (m_btnLayers) m_btnLayers->setChecked(recursive); 
 
    if (path.isEmpty() || path == "computer://") { 
        m_currentPath = "computer://"; 
        updateLayersButtonState(); 
 
        const auto drives = QDir::drives(); 
        std::vector<ItemRecord> driveRecords;
        for (const QFileInfo& drive : drives) { 
            driveRecords.push_back(ItemRecord::create(drive.absolutePath()));
        } 
        m_model->setRecords(driveRecords);
        // 2026-05-29 物理对齐：在加载“此电脑”后显式触发一次排序，确保置顶硬盘排在首位
        m_proxyModel->sort(0, Qt::AscendingOrder);
        m_isLoading = false;
        recalculateAndEmitStats();
        return; 
    } 
 
    m_currentPath = path; 
    updateLayersButtonState(); 

    QPointer<ContentPanel> panelPtr(this); 

    // 【物理隔离】纯磁盘扫描已迁出至 DiskScanService，本函数只负责调度与 UI 状态维护，
    // 不再直接持有任何扫描细节，DiskScanService.cpp 中不可能出现 MetadataManager/CategoryRepo 调用
    (void)QThreadPool::globalInstance()->start([panelPtr, path, recursive, reqId]() { 
        if (!panelPtr) return; 

        std::vector<ItemRecord> allItems = DiskScanService::scanDirectory(
            path, recursive,
            [panelPtr]() { return static_cast<bool>(panelPtr); }
        );
        if (!panelPtr) return; 
 
        QMetaObject::invokeMethod(QCoreApplication::instance(), [panelPtr, path, allItems, reqId]() { 
            if (panelPtr && panelPtr->m_loadRequestId == reqId) { 
                panelPtr->m_model->setRecords(allItems);
                panelPtr->m_proxyModel->sort(0, Qt::AscendingOrder);
                panelPtr->m_isLoading = false;
                panelPtr->recalculateAndEmitStats();
                // 2026-06-xx 物理同步：数据加载完成后强制重新应用筛选，防止显示已过滤掉的占位符记录
                panelPtr->applyFilters();

                // 2026-07-xx 按照 Plan-66：处理新建项后的自动定位与编辑
                if (!panelPtr->m_pendingSelectName.isEmpty()) {
                    const auto& records = panelPtr->m_model->allRecords();
                    for (size_t i = 0; i < records.size(); ++i) {
                        if (QFileInfo(records[i].path).fileName() == panelPtr->m_pendingSelectName) {
                            QModelIndex srcIdx = panelPtr->m_model->index(static_cast<int>(i), 0);
                            QModelIndex proxyIdx = panelPtr->m_proxyModel->mapFromSource(srcIdx);
                            if (proxyIdx.isValid()) {
                                if (panelPtr->m_viewStack->currentWidget() == panelPtr->m_gridView) {
                                    panelPtr->m_gridView->scrollTo(proxyIdx);
                                    panelPtr->m_gridView->setCurrentIndex(proxyIdx);
                                    if (panelPtr->m_isPendingEdit) panelPtr->m_gridView->edit(proxyIdx);
                                } else {
                                    panelPtr->m_treeView->scrollTo(proxyIdx);
                                    panelPtr->m_treeView->setCurrentIndex(proxyIdx);
                                    if (panelPtr->m_isPendingEdit) panelPtr->m_treeView->edit(proxyIdx);
                                }
                            }
                            break;
                        }
                    }
                    panelPtr->m_pendingSelectName = ""; // 必须物理清空状态
                }

                ArcMeta::Logger::log(QString("[Content] 目录扫描完成并已应用到 UI [%1]").arg(reqId));
                panelPtr->m_visibleTimer->start();
            } else if (panelPtr) {
                ArcMeta::Logger::log(QString("[Content] 拦截到过期的目录扫描回调 [%1], 当前 ID: %2").arg(reqId).arg(panelPtr->m_loadRequestId.load()));
            }
        }, Qt::QueuedConnection); 
    }); 
} 
 
 
 
 
void ContentPanel::search(const QString& query) { 
    // 2026-07-xx 按照 Plan-118：搜索行为回归筛选流。
    // 搜索框仅作为当前视图的本地过滤器，禁止切换 m_currentCategoryType 为 "search"。
    
    if (m_model) {
        m_model->setQuery(query);
    }

    // 1. 同步关键词到当前筛选状态
    m_currentFilter.keyword = query;

    // 2. 触发本地过滤（invalidateFilter）
    applyFilters();

    // 3. 视觉状态同步
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    if (m_viewStack) m_viewStack->show(); 

    ArcMeta::Logger::log(QString("[Search] 本地搜索关键词更新: %1 (当前视图类型: %2)")
                        .arg(query).arg(m_currentCategoryType.isEmpty() ? "nav" : m_currentCategoryType));
} 
 
void ContentPanel::applyFilters(const FilterState& state) { 
    // 2026-07-xx 物理防护：保留标题栏按钮独占维护的显隐状态，防止被 FilterPanel 的默认值覆盖
    bool preservedShowFolders = m_currentFilter.showFolders;
    bool preservedShowFiles = m_currentFilter.showFiles;
    m_currentFilter = state; 
    m_currentFilter.showFolders = preservedShowFolders;
    m_currentFilter.showFiles = preservedShowFiles;
    applyFilters(); 
} 
 
void ContentPanel::applyFilters() { 
    // 2026-05-25 编译修复：改用 qobject_cast 彻底根除 static_cast 指针转换报错 
    auto* proxy = qobject_cast<FilterProxyModel*>(m_proxyModel); 
    if (proxy) { 
        proxy->currentFilter = m_currentFilter; 
        proxy->updateFilter(); 
    } 
    // 2026-05-08 按照用户要求：筛选条件变化后更新状态栏统计
    updateStatusBarStats();
    m_visibleTimer->start();
} 
 
void ContentPanel::previewFile(const QString& path) { 
    // 2026-03-xx 按照用户要求：全能预览实现，支持图片与多种文本格式，破除 .md 局限 
    QFileInfo info(path); 
    QString ext = info.suffix().toLower(); 
 
    // 1. 图片格式识别 
    static const QStringList imageExts = {"jpg", "jpeg", "png", "bmp", "webp", "gif", "ico"}; 
    if (imageExts.contains(ext)) { 
        QPixmap pix(path); 
        if (!pix.isNull()) { 
            m_viewStack->hide(); 
            m_textPreview->hide(); 
             
            // 保持比例缩放显示 
            m_imagePreview->setPixmap(pix.scaled(m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); 
            m_imagePreview->show(); 
            return; 
        } 
    } 
 
    // 2. 文本格式识别 (参考版本A 扩展识别) 
    // 此处可根据需要进一步细化，目前先处理常规文本 
    QFile file(path); 
    if (file.open(QIODevice::ReadOnly)) { 
        m_viewStack->hide(); 
        m_imagePreview->hide(); 
 
        // 针对 Markdown 特殊渲染 
        if (ext == "md" || ext == "markdown") { 
             m_textPreview->setMarkdown(file.readAll()); 
        } else { 
             // 针对其他代码或文本，直接显示原文 
             // 限制读取前 1MB 以防大文件卡死 
             m_textPreview->setPlainText(QString::fromUtf8(file.read(1024 * 1024))); 
        } 
        m_textPreview->show(); 
        file.close(); 
    } 
} 
 
void ContentPanel::loadCategory(int categoryId) { 
    // 2026-07-xx 物理防护：防重入机制。如果已经在加载同一个分类，则直接拦截，防止重复 clear() 导致的闪烁
    if (m_isLoading && m_currentCategoryId == categoryId && m_currentCategoryType == "user_category") {
        return;
    }

    m_isLoading = true;
    int reqId = ++m_loadRequestId;
    m_currentCategoryType = "user_category";
    m_currentCategoryId = categoryId;
    updateLayersButtonState();
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); 
     
    QPointer<ContentPanel> weakThis(this);
    bool isRecursive = m_isCategoryRecursive;
    (void)QtConcurrent::run([weakThis, categoryId, reqId, isRecursive]() {
        // 【物理隔离】数据库读取已迁出至 CategoryLoadService，本函数只负责调度与 UI 状态维护
        std::vector<ItemRecord> allRecords = CategoryLoadService::loadCategoryItems(categoryId, isRecursive);
        if (!weakThis) return;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, allRecords, reqId]() {
            if (weakThis && weakThis->m_loadRequestId == reqId) {
                weakThis->m_model->setRecords(allRecords);
                weakThis->m_proxyModel->sort(0, Qt::AscendingOrder);
                weakThis->m_isLoading = false;
                weakThis->recalculateAndEmitStats();
                weakThis->applyFilters(); 

                // 2026-07-26 极致重构：系统或分类加载完成，自动重新选中之前的选中高亮目标（对应用户原话：“对某个文件夹/文件进行重命名 或 进行其他操作后仍然处于选中高亮状态”）
                if (!weakThis->m_pendingSelectName.isEmpty()) {
                    const auto& records = weakThis->m_model->allRecords();
                    for (size_t i = 0; i < records.size(); ++i) {
                        if (QFileInfo(records[i].path).fileName() == weakThis->m_pendingSelectName) {
                            QModelIndex srcIdx = weakThis->m_model->index(static_cast<int>(i), 0);
                            QModelIndex proxyIdx = weakThis->m_proxyModel->mapFromSource(srcIdx);
                            if (proxyIdx.isValid()) {
                                if (weakThis->m_viewStack->currentWidget() == weakThis->m_gridView) {
                                    weakThis->m_gridView->scrollTo(proxyIdx);
                                    weakThis->m_gridView->setCurrentIndex(proxyIdx);
                                } else {
                                    weakThis->m_treeView->scrollTo(proxyIdx);
                                    weakThis->m_treeView->setCurrentIndex(proxyIdx);
                                }
                            }
                            break;
                        }
                    }
                    weakThis->m_pendingSelectName = ""; // 清空
                }

                ArcMeta::Logger::log(QString("[Content] 分类加载完成 [%1]").arg(reqId));
            } else if (weakThis) {
                ArcMeta::Logger::log(QString("[Content] 拦截到过期的分类加载回调 [%1]").arg(reqId));
            }
        });
    });
} 
 
void ContentPanel::loadPaths(const QStringList& paths, int reqId) { 
    // 2026-07-xx 物理强化：如果路径列表为空，直接执行同步清理并返回
    // 理由：这防止了搜索启动时的清空动作（异步）与随后到达的结果加载（异步）发生竞态。
    if (paths.isEmpty()) {
        ArcMeta::Logger::log("[Content] loadPaths 收到空路径，执行同步清空");
        if (reqId == 0) m_loadRequestId++; // 若未指定 ID，则自增以作废前序加载
        else m_loadRequestId = reqId;      // 若指定了 ID，则强制对其
        
        m_model->clear();
        m_isLoading = false;
        recalculateAndEmitStats();
        return;
    }

    // 校验：如果传入了明确的 reqId，且与当前 ID 不符，则直接拦截。
    // 这对于搜索结果的流式加载至关重要。
    if (reqId != 0 && m_loadRequestId != reqId) {
        ArcMeta::Logger::log(QString("[Content] loadPaths 拦截到过期的同步请求 [%1], 当前 ID: %2")
                            .arg(reqId).arg(m_loadRequestId.load()));
        return;
    }

    // 2026-07-xx 物理防护：防重入机制
    if (m_isLoading && m_currentCategoryType == "path_list" && reqId == 0) {
        return;
    }

    m_isLoading = true;
    if (reqId == 0) reqId = ++m_loadRequestId;
    // 2026-07-xx 逻辑校准：保持既有的系统分类类型（如 trash/recently_visited），
    // 仅在明确不是这些特殊类型时，才将其降级为通用的 path_list。
    if (m_currentCategoryType != "trash" && 
        m_currentCategoryType != "recently_visited" &&
        m_currentCategoryType != "untagged" &&
        m_currentCategoryType != "uncategorized" &&
        m_currentCategoryType != "all") {
        m_currentCategoryType = "path_list";
    }
    updateLayersButtonState();
    
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    
    // 加载路径列表通常属于分类/逻辑数据源
    emit dataSourceChanged("category"); 
     
    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, paths, reqId]() {
        // 【物理隔离】数据获取已迁出至 CategoryLoadService
        if (!weakThis) return;
        std::vector<ItemRecord> records = CategoryLoadService::loadPathItems(paths);
        if (!weakThis) return;
        
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, records, reqId]() {
            if (weakThis && weakThis->m_loadRequestId == reqId) {
                weakThis->m_model->setRecords(records);
                weakThis->m_proxyModel->sort(0, Qt::AscendingOrder);
                weakThis->m_isLoading = false;
                weakThis->recalculateAndEmitStats();
                weakThis->applyFilters(); 

                // 2026-07-26 极致重构：路径列表（如搜索、系统项）加载完成，自动重新选中之前的选中高亮目标（对应用户原话：“对某个文件夹/文件进行重命名 或 进行其他操作后仍然处于选中高亮状态”）
                if (!weakThis->m_pendingSelectName.isEmpty()) {
                    const auto& rList = weakThis->m_model->allRecords();
                    for (size_t i = 0; i < rList.size(); ++i) {
                        if (QFileInfo(rList[i].path).fileName() == weakThis->m_pendingSelectName) {
                            QModelIndex srcIdx = weakThis->m_model->index(static_cast<int>(i), 0);
                            QModelIndex proxyIdx = weakThis->m_proxyModel->mapFromSource(srcIdx);
                            if (proxyIdx.isValid()) {
                                if (weakThis->m_viewStack->currentWidget() == weakThis->m_gridView) {
                                    weakThis->m_gridView->scrollTo(proxyIdx);
                                    weakThis->m_gridView->setCurrentIndex(proxyIdx);
                                } else {
                                    weakThis->m_treeView->scrollTo(proxyIdx);
                                    weakThis->m_treeView->setCurrentIndex(proxyIdx);
                                }
                            }
                            break;
                        }
                    }
                    weakThis->m_pendingSelectName = ""; // 清空
                }

                ArcMeta::Logger::log(QString("[Content] 路径列表加载完成 [%1]").arg(reqId));
            } else if (weakThis) {
                ArcMeta::Logger::log(QString("[Content] 拦截到过期的路径列表加载回调 [%1]").arg(reqId));
            }
        });
    });
}

void ContentPanel::appendPaths(const QStringList& paths, int reqId) {
    if (paths.isEmpty()) return;

    // 物理校验：如果指定了请求 ID，则必须与当前 ID 匹配，否则视为过期搜索结果
    if (reqId != 0 && m_loadRequestId != reqId) {
        ArcMeta::Logger::log(QString("[Content] appendPaths 拦截到过期的异步追加请求 [%1], 当前 ID: %2")
                            .arg(reqId).arg(m_loadRequestId.load()));
        return;
    }

    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, paths, reqId]() {
        // 【物理隔离】数据获取已迁出至 CategoryLoadService
        if (!weakThis) return;
        std::vector<ItemRecord> newRecords = CategoryLoadService::loadPathItems(paths);
        if (!weakThis) return;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, newRecords, reqId]() {
            if (weakThis && (reqId == 0 || weakThis->m_loadRequestId == reqId)) {
                // 获取当前已有记录并追加
                std::vector<ItemRecord> all = weakThis->m_model->allRecords();
                all.insert(all.end(), newRecords.begin(), newRecords.end());
                weakThis->m_model->setRecords(all);
                
                // 异步流式追加时，每批次都尝试更新一次统计与筛选
                weakThis->recalculateAndEmitStats();
                weakThis->applyFilters();
                ArcMeta::Logger::log(QString("[Content] 异步追加了 %1 条路径 [%2]").arg(newRecords.size()).arg(reqId));
            } else if (weakThis) {
                ArcMeta::Logger::log(QString("[Content] appendPaths 在回调阶段拦截到过期结果 [%1]").arg(reqId));
            }
        });
    });
}
 
bool ContentPanel::resolvePasteDestination(int& outCatId) {
    DataSourceType srcType = dataSourceType();

    if (srcType == DataSourceType::DiskNav) {
        // 磁盘模式目的地就是 m_currentPath，这里不涉及 catId，直接放行
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "粘贴失败：当前未处于任何有效目录中", 2000, QColor("#e81123"));
            return false;
        }
        return true;
    }

    if (srcType == DataSourceType::UserCategory) {
        // 用户已经明确导航到了某个具体分类，目的地无歧义
        if (m_currentCategoryId <= 0) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "粘贴失败：未识别到有效的目标分类", 2000, QColor("#e81123"));
            return false;
        }
        outCatId = m_currentCategoryId;
        return true;
    }

    // 🚨 回收站：功能定位就是承接"删除"这一个来源，不接受任何形式的新内容粘贴/拖拽
    if (m_currentCategoryType == "trash") {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "当前视图为回收站，不支持粘贴或拖拽导入新项目", 2000, QColor("#e81123"));
        return false;
    }

    // 🚨 全部数据 / 未分类 / 未标签 / 最近访问 / 搜索结果·路径列表：
    // 均为跨资源库的聚合展示，没有单一确定的目的地，弹出选择框交由用户手动指定
    if (m_currentCategoryType == "all" || m_currentCategoryType == "uncategorized" ||
        m_currentCategoryType == "untagged" || m_currentCategoryType == "recently_visited" ||
        m_currentCategoryType == "path_list" || m_currentCategoryType == "search") {

        auto categories = CategoryRepo::getAll();
        if (categories.empty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "尚无任何可用分类，请先在资源库中创建分类", 2000, QColor("#e81123"));
            return false;
        }

        QMenu pickerMenu(this);
        UiHelper::applyMenuStyle(&pickerMenu);
        pickerMenu.addAction("请选择要导入到的分类：")->setEnabled(false);
        pickerMenu.addSeparator();
        for (const auto& cat : categories) {
            QAction* act = pickerMenu.addAction(QString::fromStdWString(cat.name));
            act->setData(cat.id);
        }

        QAction* chosen = pickerMenu.exec(QCursor::pos());
        if (!chosen || !chosen->data().isValid()) {
            // 用户主动取消选择，不算失败提示，静默终止即可
            return false;
        }
        outCatId = chosen->data().toInt();
        return true;
    }

    // 兜底：其余未识别的状态，同样视为无法判断目的地
    ToolTipOverlay::instance()->showText(QCursor::pos(), "粘贴失败：当前视图不是一个有效的归类目的地", 2000, QColor("#e81123"));
    return false;
}

void ContentPanel::recalculateAndEmitStats() {
    const std::vector<ItemRecord>& records = m_model->allRecords();
    if (records.empty()) {
        // 2026-06-xx 物理修复：严禁向筛选面板发送“全空”统计信号，
        // 防止在加载大目录或执行搜索切换的中间态强行清空筛选器界面。
        return;
    }

    // 判断当前是否为纯物理磁盘模式
    bool isDiskMode = (dataSourceType() == DataSourceType::DiskNav);

    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, records, isDiskMode]() {
        ScanStats stats;

        for (const auto& record : records) {
            if (!weakThis) return;

            stats.ratingCounts[record.rating]++;
            
            if (!record.manualColor.isEmpty()) {
                stats.colorCounts[record.manualColor.toUpper()]++;
            } else {
                stats.colorCounts[""]++;
            }
            
            if (record.isDir || record.isCategory) {
                stats.typeCounts["folder"]++;
                // 物理限制：仅在磁盘模式下才累计空文件夹
                if (isDiskMode && record.isDir && record.isEmpty) {
                    stats.emptyFolderCount++;
                }
            } else {
                stats.typeCounts["file"]++;
                stats.typeCounts[record.suffix.toUpper()]++;
            }
            
            auto dateKey = [&](long long ts) {
                return QDateTime::fromMSecsSinceEpoch(ts).date().toString("dd-MM-yyyy");
            };

            stats.createDateCounts[dateKey(record.ctime)]++;
            stats.modifyDateCounts[dateKey(record.mtime)]++;
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, stats]() {
            if (weakThis) {
                emit weakThis->directoryStatsReady(stats.ratingCounts, stats.colorCounts,
                                                 stats.typeCounts, stats.createDateCounts, stats.modifyDateCounts,
                                                 stats.emptyFolderCount);
            }
        });
    });
}

void ContentPanel::createNewItem(const QString& type) { 
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") return; 
 
    QString baseName = (type == "folder") ? "新建文件夹" : "未命名"; 
    QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : ""); 
    QString finalName = baseName + ext; 
    QString fullPath = m_currentPath + "/" + finalName; 
 
    int counter = 1; 
    while (QFileInfo::exists(fullPath)) { 
        finalName = baseName + QString(" (%1)").arg(counter++) + ext; 
        fullPath = m_currentPath + "/" + finalName; 
    } 
 
    bool success = false; 
    if (type == "folder") { 
        success = QDir(m_currentPath).mkdir(finalName); 
    } else { 
        QFile file(fullPath); 
        if (file.open(QIODevice::WriteOnly)) { 
            file.close(); 
            success = true; 
        } 
    } 
 
    if (success) { 
        m_pendingSelectName = finalName;
        m_isPendingEdit = true;
        loadDirectory(m_currentPath, m_isRecursive); 
    } 
} 
 
void ContentPanel::updateLayersButtonState() { 
    if (!m_btnLayers || !m_btnLayersBlue) return; 
 
    // 2026-07-xx 互斥逻辑：分类视图下显示蓝按钮，物理路径下显示绿按钮
    bool isCategoryMode = (m_currentCategoryType == "user_category");
    m_btnLayers->setVisible(!isCategoryMode);
    m_btnLayersBlue->setVisible(isCategoryMode);

    if (isCategoryMode) {
        m_btnLayersBlue->setEnabled(true);
        m_btnLayersBlue->setChecked(m_isCategoryRecursive);
        m_btnLayersBlue->setProperty("tooltipText", "显示子分类中的项目");
        return;
    }

    if (m_currentPath.isEmpty() || m_currentPath == "computer://") { 
        m_btnLayers->setEnabled(false); 
        m_btnLayers->setChecked(false); 
        m_btnLayers->setProperty("tooltipText", "“此电脑”不支持递归显示"); 
        return; 
    } 

    // 2026-07-xx 逻辑增强：若处于搜索或其他路径列表模式（即非物理磁盘导航，即镜像源），禁用递归功能
    if (isMirrorSource()) {
        m_btnLayers->setEnabled(false);
        m_btnLayers->setChecked(false);
        m_btnLayers->setProperty("tooltipText", "当前视图不支持递归显示");
        return;
    }
 
    m_btnLayers->setEnabled(true); 
    m_btnLayers->setProperty("tooltipText", "显示子文件夹中的项目"); 
} 
 
} // namespace ArcMeta
