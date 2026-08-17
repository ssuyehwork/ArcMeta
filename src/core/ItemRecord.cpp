#include "ItemRecord.h"
#include "../meta/MetadataManager.h"
#include <QFileInfo>
#include <QDir>
#include <mutex>
#include <unordered_map>

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
