# Plan-135 全局架构彻底重构方案：内存模式纯净单向数据流

> **重构宣言**：彻底废弃所有局部补丁（包括熔断版本号、请求状态锁、主线程 I/O 探查等畸形逻辑）。基于 MVC 与单一职责原则，重建“首帧几何确定性 + 无锁只读缓存 + 局部像素直刷”的工业级全局架构。

---

## 核心架构重构清单

### 1. `src/core/ItemRecord.cpp` —— 纯内存构建，彻底剥离物理扫盘
- 移除所有对 `.arc` 包的 `QDir::entryInfoList`；
- 100% 依赖 `RuntimeMeta` 内存镜像初始化 `filename`, `suffix`, `width`, `height`。

### 2. `src/ui/models/LibraryAssetModel.h & .cpp` —— 模型只读化，移除所有状态锁
- 彻底删除 `m_requestedIcons`（死锁根源）与 `m_currentGen`；
- 缩略图返回时，**仅发射 `Qt::DecorationRole`**，绝对禁止携带 `AspectRatioRole`，杜绝全局 `doLayout`。

### 3. `src/ui/CardPainterHelper.cpp` —— 纯硬件加速渲染
- 移除所有 `thumb.scaled` CPU 软缩放，使用 `painter->drawPixmap(targetRect, thumb)` 原生绘制。

### 4. `src/ui/ContentPanel.cpp` —— 极简 50ms 视口通知
- 滚动条与定时器简化为标准单次 50ms 防抖，仅负责计算当前视口并通知模型。

---

## 核心代码实现

### 文件一：`src/core/ItemRecord.cpp`

```cpp
#include "ItemRecord.h"
#include "../meta/MetadataManager.h"
#include <QFileInfo>
#include <QDir>

namespace ArcMeta {

void ItemRecord::fromMetadata(ItemRecord& r, const RuntimeMeta& meta) {
    r.rating = meta.rating;
    r.manualColor = QString::fromStdWString(meta.manualColor);
    r.autoColor = QString::fromStdWString(meta.autoColor);
    r.tags = meta.tags;
    r.pinned = meta.pinned;
    r.encrypted = meta.encrypted;
    r.url = QString::fromStdWString(meta.url);
    r.note = QString::fromStdWString(meta.note);
    r.sha256 = QString::fromStdString(meta.sha256);
    r.width = meta.width;
    r.height = meta.height;
    r.added_at = meta.added_at;
    r.isManaged = meta.hasUserOperations();
    if (!meta.folderId.empty()) {
        r.folderId = meta.folderId;
    }
    r.palettes.clear();
    for (const auto& pe : meta.palettes) {
        r.palettes.push_back({pe.color, pe.ratio});
    }
}

ItemRecord ItemRecord::create(const QString& path, const RuntimeMeta* providedMeta, bool isFromMemory) {
    ItemRecord r;
    std::wstring wPath = MetadataManager::normalizePath(path.toStdWString());
    QString nPath = QString::fromStdWString(wPath);
    bool isArcEnd = nPath.endsWith(".arc", Qt::CaseInsensitive) || nPath.endsWith(".arc/", Qt::CaseInsensitive) || nPath.endsWith(".arc\\", Qt::CaseInsensitive);
    if (isArcEnd && (nPath.endsWith("/") || nPath.endsWith("\\"))) {
        nPath = nPath.left(nPath.length() - 1);
        wPath = nPath.toStdWString();
    }

    if (isFromMemory) {
        RuntimeMeta meta = providedMeta ? *providedMeta : MetadataManager::instance().getMeta(wPath);
        r.size = meta.fileSize;
        r.ctime = meta.ctime;
        r.mtime = meta.mtime;
        r.atime = meta.atime;
        r.folderId = meta.folderId;
        r.isDir = meta.isFolder;
        r.isManaged = true;
        r.isEmpty = false;
        r.path = nPath;

        if (!meta.baseName.empty()) {
            QString bName = QString::fromStdWString(meta.baseName);
            QString ext = QString::fromStdWString(meta.ext);
            r.filename = ext.isEmpty() ? bName : (bName + "." + ext);
            r.suffix = ext.toLower();
        } else {
            int lastSlash = std::max(nPath.lastIndexOf('\\'), nPath.lastIndexOf('/'));
            r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
            int lastDot = r.filename.lastIndexOf('.');
            r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
        }

        ItemRecord::fromMetadata(r, meta);
        return r;
    }

    // 磁盘模式分支
    std::string fid;
    long long size = 0, ctime = 0, mtime = 0, atime = 0;
    MetadataManager::fetchWinApiMetadataDirect(wPath, fid, nullptr, &size, nullptr, &ctime, &mtime, &atime);
    r.size = size;
    r.ctime = ctime;
    r.mtime = mtime;
    r.atime = atime;
    r.folderId = fid;
    r.isDir = QFileInfo(nPath).isDir();
    r.path = nPath;

    int lastSlash = std::max(nPath.lastIndexOf('\\'), nPath.lastIndexOf('/'));
    r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;

    if (r.isDir) {
        QDir sub(nPath);
        r.isEmpty = sub.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
        r.suffix = "";
    } else {
        int lastDot = nPath.lastIndexOf('.');
        r.suffix = (lastDot != -1) ? nPath.mid(lastDot + 1).toLower() : "";
    }

    return r;
}

} // namespace ArcMeta
```

---

### 文件二：`src/ui/models/LibraryAssetModel.cpp`

```cpp
#include "LibraryAssetModel.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "ModelContract.h"
#include "../ContentPanel.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFileIconProvider>
#include <QtConcurrent>

using namespace ArcMeta;

#include "../../meta/MetadataManager.h"
#include "../../meta/CategoryRepo.h"
#include "../../meta/CapsuleMediaExtractor.h"
#include "../core/UndoManager.h"
#include "../MemoryBatchRenameService.h"
#include "../core/BasicCommands.h"
#include "MediaColorExtractor.h"
#include "../../meta/FileOperationHelper.h"

LibraryAssetModel::LibraryAssetModel(QObject* parent) : ItemModelBase(parent) {
    m_iconCache.setMaxCost(2000);
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
    m_pathToIndex.reserve(records.size());
    for (int i = 0; i < static_cast<int>(m_allRecords.size()); ++i) {
        m_pathToIndex[m_allRecords[i].path] = i;
    }
    m_iconCache.setMaxCost(qMax(2000, static_cast<int>(m_allRecords.size()) + 100));
    m_metaCache.clear();
    endResetModel();
}

void LibraryAssetModel::clear() {
    beginResetModel();
    m_allRecords.clear();
    m_pathToIndex.clear();
    m_query.clear();
    m_aspectRatios.clear();
    m_metaCache.clear();
    endResetModel();
}

void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<QString> loadList;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        if (!m_iconCache.contains(rec.path)) {
            loadList.push_back(rec.path);
        }
    }

    if (loadList.empty()) return;

    QPointer<LibraryAssetModel> weakThis(this);
    for (const QString& path : loadList) {
        (void)QtConcurrent::run([weakThis, path]() {
            if (!weakThis) return;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
            
            QIcon icon;
            if (!img.isNull()) {
                icon = QIcon(QPixmap::fromImage(img));
            } else {
                icon = ShellIconManager::getFileIcon(path, 128);
            }

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon]() {
                if (weakThis) {
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole});
                        }
                    }
                }
            });
        });
    }
}

QVariant LibraryAssetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    const QString& path = record.path;

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: return record.filename;
            case 3: return (record.width > 0 && record.height > 0) ? QString("%1 x %2").arg(record.width).arg(record.height) : "-";
            case 4: return record.isDir ? "文件夹" : record.suffix.toUpper();
            case 5: {
                if (record.isDir) return "-";
                if (record.size < 1024) return QString::number(record.size) + " B";
                if (record.size < 1024 * 1024) return QString::number(record.size / 1024.0, 'f', 1) + " KB";
                return QString::number(record.size / (1024.0 * 1024.0), 'f', 1) + " MB";
            }
            case 6: return QDateTime::fromMSecsSinceEpoch(record.mtime).toString("dd-MM-yyyy HH:mm");
        }
    } else if (role == PathRole) {
        return path;
    } else if (role == TypeRole) {
        return record.isDir ? "folder" : "file";
    } else if (role == RatingRole) {
        return record.rating;
    } else if (role == ColorRole) {
        return record.manualColor;
    } else if (role == PinnedRole) {
        return record.pinned;
    } else if (role == TagsRole) {
        return record.tags;
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return 1.0;
    } else if (role == HasThumbnailRole) {
        return (record.width > 0 && record.height > 0) || UiHelper::isGraphicsFile(record.suffix);
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QIcon* cached = m_iconCache.object(path);
        if (cached) return *cached;
        return QIcon();
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
        emit dataChanged(index(rIdx, 0), index(rIdx, 0), {Qt::DecorationRole});
    }
    m_pendingUpdateRows.clear();
}
```
