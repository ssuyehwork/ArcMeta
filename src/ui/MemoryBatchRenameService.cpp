#include "MemoryBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/FileOperationHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace ArcMeta {

void MemoryBatchRenameService::execute(const std::vector<std::wstring>& originalPaths,
                                       const std::vector<std::wstring>& newNames,
                                       std::function<void(int successCount)> callback) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) {
        if (callback) callback(0);
        return;
    }

    std::vector<std::pair<std::wstring, std::wstring>> rawPairs;

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        
        QDir arcDir = oldInfo.absoluteDir(); // 直接定位到 .arc 胶囊目录
        QString newBaseName = QFileInfo(QString::fromStdWString(newNames[i])).completeBaseName();
        QString newMainPath = arcDir.filePath(QString::fromStdWString(newNames[i]));

        if (oldPath == newMainPath) {
            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir::toNativeSeparators(newMainPath).toStdWString();
            rawPairs.push_back({oldW, newW});
            continue;
        }

        // 1. 物理重命名胶囊内主资产文件（如 测试_038.ai），调用 safeRename 支持大小写中转
        if (FileOperationHelper::safeRename(oldPath, newMainPath)) {
            // 2. 物理扫描 .arc 胶囊目录，精准强杀并重命名 *_thumbnail.png，调用 safeRename 支持大小写中转
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

    // 循环结束后，统一通过 renameBatchAsync 提交批量更新，在后台线程中安全处理
    MetadataManager::instance().renameBatchAsync(rawPairs, callback);
}

} // namespace ArcMeta
