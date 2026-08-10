#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include "CapsuleMediaExtractor.h"
#include "../util/DiskMediaExtractor.h"
#include <QFileInfo>
#include <QDir>

namespace ArcMeta {

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;

    for (const QString& newPath : newImportedPaths) {
        QFileInfo newFi(newPath);
        std::wstring wNewPath = MetadataManager::normalizePath(newPath.toStdWString());

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& cachedWPath, const RuntimeMeta& rm) {
            if (cachedWPath == wNewPath) return;

            QFileInfo cachedFi(QString::fromStdWString(cachedWPath));
            if (cachedFi.fileName().toLower() == newFi.fileName().toLower() && cachedFi.size() == newFi.size()) {
                DuplicateConflictGroup group;

                // 1. 已存在项信息
                group.existingItem.folderId = QString::fromStdString(rm.folderId);
                group.existingItem.path = QString::fromStdWString(cachedWPath);
                group.existingItem.filename = cachedFi.fileName();
                group.existingItem.size = cachedFi.size();
                group.existingItem.width = rm.width;
                group.existingItem.height = rm.height;
                if (!rm.tags.isEmpty()) {
                    group.existingItem.tagHint = rm.tags.join(", ");
                }
                group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(QString::fromStdWString(cachedWPath));

                // 2. 新文件项信息
                group.newItem.path = newPath;
                group.newItem.filename = newFi.fileName();
                group.newItem.size = newFi.size();
                // 新文件还未入库，宽高可以从 QImage 临时获取
                QImage newThumb = DiskMediaExtractor::getDiskThumbnail(newPath, 256);
                group.newItem.thumbnail = newThumb;
                group.newItem.width = newThumb.width();
                group.newItem.height = newThumb.height();

                conflicts.push_back(group);
            }
        });
    }
    return conflicts;
}

} // namespace ArcMeta
