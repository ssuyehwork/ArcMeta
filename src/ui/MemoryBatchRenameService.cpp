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

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(originalPaths[i]);
        QFileInfo oldInfo(oldPath);

        // 目标路径：保持在原 .arc 胶囊文件夹内
        QString finalDir = oldInfo.absolutePath();
        QString newPathStr = QDir(finalDir).filePath(QString::fromStdWString(newNames[i]));

        if (oldPath == newPathStr) {
            successCount++;
            continue;
        }

        // 1. 物理重命名胶囊内主资产文件
        if (QFile::rename(oldPath, newPathStr)) {
            successCount++;

            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir(finalDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();

            // 2. 更新内存数据库与倒排索引（.arc 胶囊内 _thumbnail.png 固定存在，无需改动文件名，只需更新主资产索引）
            MetadataManager::instance().renameItem(oldW, newW);

            // 3. 同步更新分类映射表 path_hint 引用
            CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
        }
    }

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();

    return successCount;
}

} // namespace ArcMeta
