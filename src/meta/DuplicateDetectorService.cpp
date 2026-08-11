#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QDebug>

namespace ArcMeta {

std::string DuplicateDetectorService::calculateFastHash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return "";
    }
    qint64 size = file.size();
    if (size <= 8192) {
        QByteArray data = file.readAll();
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
    }

    QByteArray buffer;
    buffer.reserve(8192);

    // 读取头部 4KB
    buffer.append(file.read(4096));

    // 读取尾部 4KB
    if (file.seek(size - 4096)) {
        buffer.append(file.read(4096));
    }

    return QCryptographicHash::hash(buffer, QCryptographicHash::Sha256).toHex().toStdString();
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
        if (!newFi.exists() || !newFi.isFile()) continue;

        qint64 newSize = newFi.size();
        std::wstring wNewPath = MetadataManager::normalizePath(newPath.toStdWString());
        
        // 我们需要缓存在这轮循环中新算出的哈希
        bool hasNewFastHash = false;
        std::string newFastHash = "";

        bool hasNewSha256 = false;
        std::string newSha256 = "";

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& cachedWPath, const RuntimeMeta& rm) {
            if (cachedWPath == wNewPath) return;
            
            // 第一级（体积排除）
            if (rm.isFolder || rm.isTrash) return;
            if (rm.fileSize != newSize) return;

            // 第二级（快速采样 Hash 比对）
            if (!hasNewFastHash) {
                newFastHash = calculateFastHash(newPath);
                hasNewFastHash = true;
            }
            if (newFastHash.empty()) return;

            QString cachedPath = QString::fromStdWString(cachedWPath);
            std::string cachedFastHash = calculateFastHash(cachedPath);
            if (cachedFastHash.empty() || cachedFastHash != newFastHash) return;

            // 第三级（全量 SHA256 确认）
            if (!hasNewSha256) {
                newSha256 = calculateFullSha256(newPath);
                hasNewSha256 = true;
            }
            if (newSha256.empty()) return;

            std::string cachedSha256 = rm.sha256;
            if (cachedSha256.empty()) {
                // 如果历史文件缺失 sha256，对历史文件计算一次并立即持久化落盘
                cachedSha256 = calculateFullSha256(cachedPath);
                if (!cachedSha256.empty()) {
                    MetadataManager::instance().setSha256(cachedWPath, cachedSha256, false);
                }
            }

            if (cachedSha256.empty() || cachedSha256 != newSha256) return;

            // 第四级（生成冲突项，不加载图片）
            DuplicateConflictGroup group;

            // 1. 已存在项信息
            group.existingItem.folderId = QString::fromStdString(rm.folderId);
            group.existingItem.path = QString::fromStdWString(cachedWPath);
            group.existingItem.filename = QFileInfo(cachedPath).fileName();
            group.existingItem.size = rm.fileSize;
            group.existingItem.width = rm.width;
            group.existingItem.height = rm.height;
            group.existingItem.sha256 = cachedSha256;
            if (!rm.tags.isEmpty()) {
                group.existingItem.tagHint = rm.tags.join(", ");
            }
            // 🚨 注意：禁止在该处进行缩略图提取与解码

            // 2. 新文件项信息
            group.newItem.path = newPath;
            group.newItem.filename = newFi.fileName();
            group.newItem.size = newSize;
            group.newItem.sha256 = newSha256;

            conflicts.push_back(group);
        });
    }
    return conflicts;
}

} // namespace ArcMeta
