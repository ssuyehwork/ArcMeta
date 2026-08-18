#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include "CapsuleMediaExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QImageReader>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace QuarkMeta {

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;
    if (newImportedPaths.isEmpty()) return conflicts;

    // 预索引：在进入大循环前，对已缓存的资产进行一次性建图索引，降低时间复杂度至 O(M)
    std::unordered_map<long long, std::vector<std::pair<std::wstring, RuntimeMeta>>> sizeIndexMap;
    std::unordered_map<std::wstring, std::vector<std::pair<std::wstring, RuntimeMeta>>> nameIndexMap;

    MetadataManager::instance().forEachCachedItem([&](const std::wstring& existPathW, const RuntimeMeta& meta) {
        if (meta.isFolder || meta.isTrash) return;

        // 大小索引
        sizeIndexMap[meta.fileSize].push_back({existPathW, meta});

        // 文件名索引 (统一转小写以进行不区分大小写的比对)
        QFileInfo info(QString::fromStdWString(existPathW));
        std::wstring lowerName = info.fileName().toStdWString();
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        nameIndexMap[lowerName].push_back({existPathW, meta});
    });

    for (const QString& newPath : newImportedPaths) {
        QFileInfo newInfo(newPath);
        if (!newInfo.exists() || newInfo.isDir()) continue;

        qint64 size = newInfo.size();
        QString fileName = newInfo.fileName();
        std::wstring lowerNewName = fileName.toStdWString();
        std::transform(lowerNewName.begin(), lowerNewName.end(), lowerNewName.begin(), ::tolower);

        // 1. 计算新文件的 SHA-256 哈希
        QFile file(newPath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) continue;
        QString sha256Hex = QString(hash.result().toHex()).toLower();
        file.close();

        // 2. 获取新文件的分辨率
        QImageReader reader(newPath);
        QSize newImgSize = reader.size();
        int newWidth = newImgSize.width();
        int newHeight = newImgSize.height();

        bool alreadyMatched = false;   // 标记这个新文件是否已经找到过一次重复

        // 一重判定：文件大小相同且哈希相同
        auto sizeIt = sizeIndexMap.find(size);
        if (sizeIt != sizeIndexMap.end()) {
            for (const auto& pair : sizeIt->second) {
                const std::wstring& existPathW = pair.first;
                const RuntimeMeta& meta = pair.second;
                if (QString::fromStdWString(existPathW) == newPath) continue;

                // 只有大小相同时，才对已有文件在后台进行 SHA-256 哈希抽取比对，将 I/O 开销降到最低
                QString existSha;
                if (!meta.sha256.empty()) {
                    existSha = QString::fromStdString(meta.sha256).toLower();
                } else {
                    QFile existFile(QString::fromStdWString(existPathW));
                    if (existFile.open(QIODevice::ReadOnly)) {
                        QCryptographicHash existHash(QCryptographicHash::Sha256);
                        if (existHash.addData(&existFile)) {
                            existSha = QString(existHash.result().toHex()).toLower();
                        }
                        existFile.close();
                    }
                }

                if (!existSha.isEmpty() && existSha == sha256Hex) {
                    DuplicateConflictGroup group;
                    group.existingItem.folderId = QString::fromStdString(meta.folderId);
                    group.existingItem.path = QString::fromStdWString(existPathW);
                    group.existingItem.filename = QFileInfo(QString::fromStdWString(existPathW)).fileName();
                    group.existingItem.width = meta.width;
                    group.existingItem.height = meta.height;
                    group.existingItem.size = meta.fileSize;
                    group.existingItem.tagHint = meta.tags.isEmpty() ? "" : meta.tags.first();
                    group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(QString::fromStdWString(existPathW));

                    group.newItem.path = newPath;
                    group.newItem.filename = fileName;
                    group.newItem.width = newWidth; 
                    group.newItem.height = newHeight;
                    group.newItem.size = size;
                    group.newItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnail(newPath, 512);

                    conflicts.push_back(group);
                    alreadyMatched = true;
                    break;   // 找到第一个就够了，不再比对该 size 桶里剩下的旧文件
                }
            }
        }

        // 二重判定：只有一重判定完全没找到匹配时才需要执行
        if (!alreadyMatched) {
            auto nameIt = nameIndexMap.find(lowerNewName);
            if (nameIt != nameIndexMap.end()) {
                for (const auto& pair : nameIt->second) {
                    const std::wstring& existPathW = pair.first;
                    const RuntimeMeta& meta = pair.second;
                    if (QString::fromStdWString(existPathW) == newPath) continue;

                    if (meta.width > 0 && meta.height > 0 && meta.width == newWidth && meta.height == newHeight) {
                        DuplicateConflictGroup group;
                        
                        group.existingItem.folderId = QString::fromStdString(meta.folderId);
                        group.existingItem.path = QString::fromStdWString(existPathW);
                        group.existingItem.filename = QFileInfo(QString::fromStdWString(existPathW)).fileName();
                        group.existingItem.width = meta.width;
                        group.existingItem.height = meta.height;
                        group.existingItem.size = meta.fileSize;
                        group.existingItem.tagHint = meta.tags.isEmpty() ? "" : meta.tags.first();
                        group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(QString::fromStdWString(existPathW));

                        group.newItem.path = newPath;
                        group.newItem.filename = fileName;
                        group.newItem.width = newWidth; 
                        group.newItem.height = newHeight;
                        group.newItem.size = size;
                        group.newItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnail(newPath, 512);

                        conflicts.push_back(group);
                        break;   // 同样找到第一个就够了
                    }
                }
            }
        }
    }

    return conflicts;
}

} // namespace QuarkMeta
