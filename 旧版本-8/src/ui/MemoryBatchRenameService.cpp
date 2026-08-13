#include "MemoryBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/FileOperationHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QtConcurrent>

namespace ArcMeta {

void MemoryBatchRenameService::execute(const std::vector<std::wstring>& originalPaths, 
                                       const std::vector<std::wstring>& newNames,
                                       std::function<void(int successCount)> callback) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) {
        if (callback) callback(0);
        return;
    }

    // 🚨 将物理文件 I/O 整体抛入后台线程，彻底释放 UI 主线程
    (void)QtConcurrent::run([originalPaths, newNames, callback]() {
        std::vector<std::pair<std::wstring, std::wstring>> rawPairs;

        for (size_t i = 0; i < originalPaths.size(); ++i) {
            QString oldPath = QString::fromStdWString(originalPaths[i]);
            QFileInfo oldInfo(oldPath);
            
            QDir arcDir = oldInfo.absoluteDir();
            QString newBaseName = QFileInfo(QString::fromStdWString(newNames[i])).completeBaseName();
            QString newMainPath = arcDir.filePath(QString::fromStdWString(newNames[i]));

            if (oldPath == newMainPath) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir::toNativeSeparators(newMainPath).toStdWString();
                rawPairs.push_back({oldW, newW});
                continue;
            }

            // 1. 物理重命名主资产文件 (后台线程执行)
            if (FileOperationHelper::safeRename(oldPath, newMainPath)) {
                // 2. 物理扫描并重命名缩略图 (后台线程执行)
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                for (const QString& oldThumbName : thumbFiles) {
                    QString oldThumbAbsPath = arcDir.filePath(oldThumbName);
                    QString newThumbAbsPath = arcDir.filePath(newBaseName + "_thumbnail.png");
                    
                    if (oldThumbAbsPath != newThumbAbsPath) {
                        FileOperationHelper::safeRename(oldThumbAbsPath, newThumbAbsPath);
                    }
                }

                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir::toNativeSeparators(newMainPath).toStdWString();
                rawPairs.push_back({oldW, newW});
            }
        }

        // 物理改名完成后，接续提交数据库批量大事务
        MetadataManager::instance().renameBatchAsync(rawPairs, callback);
    });
}

} // namespace ArcMeta
