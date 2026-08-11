#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>

namespace ArcMeta {

std::string DuplicateDetectorService::calculateFastHash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return "";
    }
    qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (size <= 8192) {
        hash.addData(file.readAll());
    } else {
        QByteArray buffer = file.read(4096);
        if (file.seek(size - 4096)) {
            buffer.append(file.read(4096));
        }
        hash.addData(buffer);
    }
    return hash.result().toHex().toStdString();
}

std::string DuplicateDetectorService::calculateFullSha256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return "";
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        return hash.result().toHex().toStdString();
    }
    return "";
}

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;
    
    for (const QString& newPath : newImportedPaths) {
        QFileInfo newFi(newPath);
        if (!newFi.exists()) continue;
        qint64 newSize = newFi.size();
        std::wstring wNewPath = MetadataManager::normalizePath(newPath.toStdWString());
        
        bool hasNewFastHash = false;
        std::string newFastHash;
        bool hasNewSha256 = false;
        std::string newSha256;

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& cachedWPath, const RuntimeMeta& rm) {
            if (cachedWPath == wNewPath) return;
            
            // 第一级：体积比对
            if (rm.fileSize != newSize) return;

            // 第二级：快速采样 Hash 比对
            if (!hasNewFastHash) {
                newFastHash = calculateFastHash(newPath);
                hasNewFastHash = true;
            }
            if (newFastHash.empty()) return;

            std::string existingFastHash = calculateFastHash(QString::fromStdWString(cachedWPath));
            if (existingFastHash.empty() || existingFastHash != newFastHash) return;

            // 第三级：全量 SHA256 比对
            if (!hasNewSha256) {
                newSha256 = calculateFullSha256(newPath);
                hasNewSha256 = true;
            }
            if (newSha256.empty()) return;

            std::string existingSha256 = rm.sha256;
            if (existingSha256.empty()) {
                existingSha256 = calculateFullSha256(QString::fromStdWString(cachedWPath));
                if (!existingSha256.empty()) {
                    MetadataManager::instance().setSha256(cachedWPath, existingSha256, false);
                }
            }
            if (existingSha256.empty() || existingSha256 != newSha256) return;

            // 第四级：冲突收集与 UI 解耦
            QFileInfo cachedFi(QString::fromStdWString(cachedWPath));
            DuplicateConflictGroup group;

            // 1. 已存在项信息
            group.existingItem.folderId = QString::fromStdString(rm.folderId);
            group.existingItem.path = QString::fromStdWString(cachedWPath);
            group.existingItem.filename = cachedFi.fileName();
            group.existingItem.size = rm.fileSize;
            group.existingItem.width = rm.width;
            group.existingItem.height = rm.height;
            group.existingItem.sha256 = existingSha256;
            if (!rm.tags.isEmpty()) {
                group.existingItem.tagHint = rm.tags.join(", ");
            }

            // 2. 新文件项信息
            group.newItem.path = newPath;
            group.newItem.filename = newFi.fileName();
            group.newItem.size = newSize;
            group.newItem.sha256 = newSha256;
            group.newItem.width = 0;
            group.newItem.height = 0;

            conflicts.push_back(group);
        });
    }
    return conflicts;
}

} // namespace ArcMeta
