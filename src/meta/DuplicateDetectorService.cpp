#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include "CapsuleMediaExtractor.h"
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFile>

namespace ArcMeta {

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;

    for (const QString& newPath : newImportedPaths) {
        QFileInfo newInfo(newPath);
        if (!newInfo.exists() || newInfo.isDir()) continue;

        qint64 size = newInfo.size();
        QString fileName = newInfo.fileName();

        // 计算 SHA-256 哈希
        QFile file(newPath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) continue;
        QString sha256Hex = QString(hash.result().toHex()).toLower();
        file.close();

        // 检索数据库中是否存在相同指纹/同名同大小同尺寸的项目
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& existPathW, const RuntimeMeta& meta) {
            QString existPath = QString::fromStdWString(existPathW);
            if (existPath == newPath || meta.isFolder || meta.isTrash) return;

            QFileInfo existInfo(existPath);
            if (existInfo.size() == size && (meta.sha256 == sha256Hex || existInfo.fileName().compare(fileName, Qt::CaseInsensitive) == 0)) {
                DuplicateConflictGroup group;

                // 填充已有文件信息
                group.existingItem.folderId = QString::fromStdString(meta.folderId);
                group.existingItem.path = existPath;
                group.existingItem.filename = existInfo.fileName();
                group.existingItem.width = meta.width;
                group.existingItem.height = meta.height;
                group.existingItem.size = existInfo.size();
                group.existingItem.tagHint = meta.tags.isEmpty() ? "" : meta.tags.first();
                group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(existPath);

                // 填充新导入文件信息
                group.newItem.path = newPath;
                group.newItem.filename = fileName;
                group.newItem.width = meta.width;
                group.newItem.height = meta.height;
                group.newItem.size = size;
                group.newItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnail(newPath, 512);

                conflicts.push_back(group);
            }
        });
    }

    return conflicts;
}

} // namespace ArcMeta
