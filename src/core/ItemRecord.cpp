#include "ItemRecord.h"
#include "../meta/MetadataManager.h"
#include <QFileInfo>
#include <QDir>
#include <mutex>
#include <unordered_map>

namespace QuarkMeta {

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

    RuntimeMeta meta;
    if (providedMeta) {
        meta = *providedMeta;
    } else if (isFromMemory) {
        meta = MetadataManager::instance().getMeta(wPath);
    }

    if (isFromMemory) {
        // 🚨【真·纯内存模式】：100% 从内存 RuntimeMeta 镜像读取，严禁任何物理磁盘 I/O
        r.size = meta.fileSize;
        r.ctime = meta.ctime;
        r.mtime = meta.mtime;
        r.atime = meta.atime;
        r.folderId = meta.folderId;
        r.isDir = meta.isFolder;
        r.isManaged = true;
        r.isEmpty = false;
        r.path = nPath;

        // 直接从内存元数据注入真实素材文件名与后缀
        if (!meta.baseName.empty()) {
            QString baseNameStr = QString::fromStdWString(meta.baseName);
            r.suffix = QString::fromStdWString(meta.ext).toLower();
            // 严禁对已包含扩展名的文件名进行二次拼接（消除 .svg.svg 双后缀 Bug）
            if (!r.suffix.isEmpty() && !baseNameStr.endsWith("." + r.suffix, Qt::CaseInsensitive)) {
                r.filename = baseNameStr + "." + r.suffix;
            } else {
                r.filename = baseNameStr;
            }
        } else {
            int lastSlash = std::max(nPath.lastIndexOf('\\'), nPath.lastIndexOf('/'));
            r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
            int lastDot = r.filename.lastIndexOf('.');
            r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
        }

        ItemRecord::fromMetadata(r, meta);
        return r;
    }

    // 磁盘模式分支（保持原有 Win32 探测）
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

} // namespace QuarkMeta
