#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace ArcMeta {

std::vector<ItemRecord> DiskScanService::scanDirectory(const QString& path,
                                                        bool recursive,
                                                        const std::function<bool()>& shouldContinue) {
    std::vector<ItemRecord> allItems;

    std::function<void(const QString&, bool)> scanDir;
    scanDir = [&](const QString& p, bool rec) {
        QDir dir(p);
        if (!dir.exists()) return;

        // 自动加载该文件夹下的 AmMetaJson 离散标记缓存
        AmMetaJson jsonCache(p.toStdWString());
        jsonCache.load();
        const auto& cachedItems = jsonCache.items();

        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;

            QString absPath = info.absoluteFilePath();
            ItemRecord itemRec = ItemRecord::create(absPath, nullptr, false);

            // 如果该物理文件在 ArcMeta.cache 中有对应的离散打标缓存，将其无缝还原到 ItemRecord 中
            std::wstring fileName = info.fileName().toStdWString();
            auto it = cachedItems.find(fileName);
            if (it != cachedItems.end()) {
                itemRec.rating = it->second.rating;
                itemRec.manualColor = QString::fromStdWString(it->second.color);
                itemRec.pinned = it->second.pinned;
                itemRec.note = QString::fromStdWString(it->second.note);
                itemRec.url = QString::fromStdWString(it->second.url);
                itemRec.tags.clear();
                for (const auto& t : it->second.tags) {
                    itemRec.tags.append(QString::fromStdWString(t));
                }
                itemRec.width = it->second.width;
                itemRec.height = it->second.height;
                itemRec.autoColor = QString::fromStdWString(it->second.autoColor);
                itemRec.added_at = it->second.addedAt;

                itemRec.palettes.clear();
                for (const auto& pe : it->second.palettes) {
                    itemRec.palettes.push_back({pe.color, pe.ratio});
                }
            }

            allItems.push_back(itemRec);

            if (rec && info.isDir()) {
                scanDir(absPath, true);
            }
        }
    };

    scanDir(path, recursive);
    return allItems;
}

} // namespace ArcMeta
