#include "DiskBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "../meta/FileOperationHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>

namespace ArcMeta {

void DiskBatchRenameService::execute(const std::vector<std::wstring>& originalPaths,
                                     const std::vector<std::wstring>& newNames,
                                     DiskOperationMode mode,
                                     const QString& targetDir,
                                     std::function<void(int successCount)> callback) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) {
        if (callback) callback(0);
        return;
    }

    int successCount = 0;
    std::vector<std::pair<std::wstring, std::wstring>> rawPairs;

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(originalPaths[i]);
        QFileInfo oldInfo(oldPath);

        QString destDir = (mode == DiskOperationMode::Rename) ? oldInfo.absolutePath() : targetDir;
        QString newPathStr = QDir(destDir).filePath(QString::fromStdWString(newNames[i]));

        bool ok = false;
        if (mode == DiskOperationMode::Copy) {
            ok = QFile::copy(oldPath, newPathStr);
        } else if (mode == DiskOperationMode::Move) {
            ok = FileOperationHelper::safeMove(oldPath, newPathStr);
        } else { // Rename
            ok = FileOperationHelper::safeRename(oldPath, newPathStr);
        }

        if (ok) {
            successCount++;

            // 同步对 .arcmeta/disk_thumbs/ 中的哈希缩略图进行重命名/迁移/复制
            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPath);
            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);

            if (QFile::exists(oldThumbHashPath)) {
                if (mode == DiskOperationMode::Copy) {
                    QFile::copy(oldThumbHashPath, newThumbHashPath);
                } else if (mode == DiskOperationMode::Move) {
                    FileOperationHelper::safeMove(oldThumbHashPath, newThumbHashPath);
                } else { // Rename
                    FileOperationHelper::safeRename(oldThumbHashPath, newThumbHashPath);
                }
            }

            if (mode != DiskOperationMode::Copy) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir(destDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();
                rawPairs.push_back({oldW, newW});
            }
        }
    }

    // 循环结束后，统一通过 renameBatchAsync 提交批量更新
    if (mode == DiskOperationMode::Copy) {
        // 对于 Copy 模式，安全回切 UI 线程直接触发回调
        QMetaObject::invokeMethod(qApp, [callback, successCount]() {
            if (callback) callback(successCount);
        }, Qt::QueuedConnection);
    } else {
        MetadataManager::instance().renameBatchAsync(rawPairs, callback);
    }
}

} // namespace ArcMeta
