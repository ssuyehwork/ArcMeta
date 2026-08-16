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

    // 1. 物理属性采样 (零 I/O 核心)
    // 🚨 [双轨不隔离极简解耦重构]: 磁盘导航模式下（isFromMemory == false）100% 拒绝穿透去读受控库数据库！
    RuntimeMeta meta;
    bool isArcPath = isFromMemory && (wPath.find(L".arc") != std::wstring::npos);
    if (providedMeta) {
        meta = *providedMeta;
    } else if (isArcPath) {
        meta = MetadataManager::instance().getMeta(wPath);
    }

    if (meta.folderId.empty() || (meta.ctime == 0 && meta.mtime == 0)) {
        std::string fid;
        long long size = 0, ctime = 0, mtime = 0, atime = 0;
        MetadataManager::fetchWinApiMetadataDirect(wPath, fid, nullptr, &size, nullptr, &ctime, &mtime, &atime);
        r.size = size;
        r.ctime = ctime;
        r.mtime = mtime;
        r.atime = atime;
        
        if (isFromMemory && wPath.find(L".arc") != std::wstring::npos) {
            r.folderId = MetadataManager::instance().getFolderIdSync(wPath);
        } else {
            r.folderId = fid;
        }
        
        r.isDir = QFileInfo(nPath).isDir();
    } else {
        r.size = meta.fileSize;
        r.ctime = meta.ctime;
        r.mtime = meta.mtime;
        r.atime = meta.atime;
        r.folderId = meta.folderId;
        r.isDir = meta.isFolder;
    }

    r.path = nPath;
    {
        int lastSlash = nPath.lastIndexOf('\\');
        if (lastSlash == -1) lastSlash = nPath.lastIndexOf('/');
        r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
    }

    // 2. 核心元数据注入 (确保 width/height/palettes 物理对齐)
    if (providedMeta || isArcPath) {
        ItemRecord::fromMetadata(r, meta);
    } else {
        r.rating = 0;
        r.isManaged = false;
        r.pinned = false;
        r.encrypted = false;
        r.width = 0;
        r.height = 0;
        r.added_at = 0;
    }

    if (r.isDir) {
        // 从数据库加载持久化的进度值
        if (isFromMemory) {
            r.registrationProgress = MetadataManager::instance().getProgressFromDb(wPath);
        } else {
            r.registrationProgress = -1.0;
        }

        if (providedMeta || (isFromMemory && meta.isManaged)) {
            r.isEmpty = false; 
        } else {
            QDir sub(nPath);
            r.isEmpty = sub.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(); // 仅磁盘模式生效
        }
        r.suffix = ""; 
    } else {
        int lastDot = nPath.lastIndexOf('.');
        r.suffix = (lastDot != -1) ? nPath.mid(lastDot + 1).toLower() : "";
    }

    // 3. 内存模式下彻底穿透包内查找主素材文件，将其真实文件名与扩展名注入 ItemRecord
    if (isFromMemory && r.isDir && nPath.endsWith(".arc", Qt::CaseInsensitive)) {
        QDir arcDir(nPath);
        QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : files) {
            QString fn = fi.fileName();
            if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
            if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
            if (fn.compare("metadata.scch", Qt::CaseInsensitive) == 0) continue;
            
            r.filename = fn;
            r.suffix = fi.suffix().toLower();
            break;
        }
    }

    return r;
}

} // namespace ArcMeta
