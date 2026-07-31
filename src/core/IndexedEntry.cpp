#include "IndexedEntry.h"
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
    r.width = meta.width;
    r.height = meta.height;
    r.added_at = meta.added_at;
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
    // 🚨 [双轨不隔离违规点-1]: 磁盘导航模式下通过 MetadataManager::getMeta 直接读取了托管库 SQLite 数据库
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
        
        // 🚨 内存数据库模式唯一ID体系重构：优先解析和提取 Base36 ID，如果是磁盘普通路径，则复用本轮采样已取得的 fid，彻底消除双重 I/O 冗余
        size_t pos = wPath.find(L".arc");
        if (pos != std::wstring::npos) {
            r.fileId = MetadataManager::instance().getFileIdSync(wPath);
        } else {
            r.fileId = fid;
        }
        
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

        // 严格遵循规则：空文件夹判定只应用于磁盘模式！
        if (providedMeta || meta.isManaged) {
            r.isEmpty = false; // 镜像/托管模式下强行禁用空文件夹逻辑
        } else {
            QDir sub(nPath);
            r.isEmpty = sub.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(); // 仅磁盘模式生效
        }
        r.suffix = ""; // 文件夹不应有扩展名后缀
    } else {
        int lastDot = nPath.lastIndexOf('.');
        r.suffix = (lastDot != -1) ? nPath.mid(lastDot + 1).toLower() : "";
    }
    return r;
}

} // namespace ArcMeta
