#include "MemoryBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace ArcMeta {

int MemoryBatchRenameService::execute(const std::vector<std::wstring>& originalPaths, 
                                      const std::vector<std::wstring>& newNames) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) return 0;

    int successCount = 0;
    MetadataManager::instance().setInternalOperating(true);

    std::vector<std::pair<std::wstring, std::wstring>> renamePairs;

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        
        QDir arcDir = oldInfo.absoluteDir(); // 直接定位到 .arc 胶囊目录
        QString newBaseName = QFileInfo(QString::fromStdWString(newNames[i])).completeBaseName();
        QString newMainPath = arcDir.filePath(QString::fromStdWString(newNames[i]));

        if (oldPath == newMainPath) {
            successCount++;
            continue;
        }

        // 1. 物理重命名胶囊内主资产文件（如 测试_038.ai）
        if (QFile::rename(oldPath, newMainPath)) {
            successCount++;

            // 2. 物理扫描 .arc 胶囊目录，精准强杀并重命名 *_thumbnail.png
            QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
            for (const QString& oldThumbName : thumbFiles) {
                QString oldThumbAbsPath = arcDir.filePath(oldThumbName);
                QString newThumbAbsPath = arcDir.filePath(newBaseName + "_thumbnail.png");
                
                if (oldThumbAbsPath != newThumbAbsPath) {
                    QFile::rename(oldThumbAbsPath, newThumbAbsPath);
                }
            }

            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir::toNativeSeparators(newMainPath).toStdWString();

            // 收集重命名路径对
            renamePairs.push_back({oldW, newW});
            CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
        }
    }

    // 关键修复：统一交由 renameItemsBatch 单线程大事务落盘，彻底解决线程风暴死锁
    if (!renamePairs.empty()) {
        MetadataManager::instance().renameItemsBatch(renamePairs);
    }

    MetadataManager::instance().setInternalOperating(false);

    return successCount;
}

} // namespace ArcMeta
