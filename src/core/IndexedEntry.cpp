#include "IndexedEntry.h"
#include "../meta/MetadataManager.h"
#include <QFileInfo>
#include <QDir>

namespace ArcMeta {

void ItemRecord::fromMetadata(ItemRecord& r, const RuntimeMeta& meta) {
    r.rating = meta.rating;
    r.color = QString::fromStdWString(meta.color);
    r.tags = meta.tags;
    r.pinned = meta.pinned;
    r.encrypted = meta.encrypted;
    r.url = QString::fromStdWString(meta.url);
    r.note = QString::fromStdWString(meta.note);
    r.width = meta.width;
    r.height = meta.height;
    r.isManaged = meta.hasUserOperations();
    if (!meta.fileId128.empty()) {
        r.fileId = meta.fileId128;
    }
    r.palettes.clear();
    for (const auto& pe : meta.palettes) {
        r.palettes.push_back({pe.color, pe.ratio});
    }
}

ItemRecord ItemRecord::create(const QString& path, const RuntimeMeta* providedMeta) {
    ItemRecord r;
    std::wstring wPath = MetadataManager::normalizePath(path.toStdWString());
    QString nPath = QString::fromStdWString(wPath);

    // 1. 物理属性采样 (零 I/O 核心)
    RuntimeMeta meta;
    if (providedMeta) {
        meta = *providedMeta;
    } else {
        meta = MetadataManager::instance().getMeta(wPath);
    }

    // Plan-124: 只有在内存缓存缺失物理时间戳时，才触发 fetchWinApiMetadataDirect
    if (meta.fileId128.empty() || (meta.ctime == 0 && meta.mtime == 0)) {
        std::string fid;
        long long size = 0, ctime = 0, mtime = 0, atime = 0;
        MetadataManager::fetchWinApiMetadataDirect(wPath, fid, nullptr, &size, nullptr, &ctime, &mtime, &atime);
        r.size = size;
        r.ctime = ctime;
        r.mtime = mtime;
        r.atime = atime;
        r.fileId = fid;
        r.isDir = QFileInfo(nPath).isDir();
    } else {
        r.size = meta.fileSize;
        r.ctime = meta.ctime;
        r.mtime = meta.mtime;
        r.atime = meta.atime;
        r.fileId = meta.fileId128;
        r.isDir = meta.isFolder;
    }

    r.path = nPath;
    {
        int lastSlash = nPath.lastIndexOf('\\');
        if (lastSlash == -1) lastSlash = nPath.lastIndexOf('/');
        r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
    }

    // 2. 核心元数据注入 (确保 width/height/palettes 物理对齐)
    ItemRecord::fromMetadata(r, meta);

    if (r.isDir) {
        // 从数据库加载持久化的进度值
        r.registrationProgress = MetadataManager::instance().getProgressFromDb(wPath);

        // 只有当明确处于“镜像模式”（providedMeta != nullptr）或项已由数据库托管时，才信任内存索引。
        // 物理路径导航模式下，若项未录入或缓存未命中，必须执行磁盘 I/O 探测以确保正确性。
        if (providedMeta || meta.isManaged) {
            r.isEmpty = !MetadataManager::instance().hasChildrenInCache(wPath);
        } else {
            QDir sub(nPath);
            r.isEmpty = sub.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
        }
        r.suffix = ""; // 文件夹不应有扩展名后缀
    } else {
        int lastDot = nPath.lastIndexOf('.');
        r.suffix = (lastDot != -1) ? nPath.mid(lastDot + 1).toLower() : "";
    }
    return r;
}

} // namespace ArcMeta
