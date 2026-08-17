#include "DiskBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "../meta/FileOperationHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QtConcurrent>

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

    // 🚨 将物理文件复制/移动/改名 I/O 整体抛入后台线程，彻底释放 UI 主线程
    (void)QtConcurrent::run([originalPaths, newNames, mode, targetDir, callback]() {
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

                // 同步处理 Hash 缩略图 (后台线程执行)
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

        if (mode == DiskOperationMode::Copy) {
            QMetaObject::invokeMethod(qApp, [callback, successCount]() {
                if (callback) callback(successCount);
            }, Qt::QueuedConnection);
        } else {
            MetadataManager::instance().renameBatchAsync(rawPairs, callback);
        }
    });
}

} // namespace ArcMeta
