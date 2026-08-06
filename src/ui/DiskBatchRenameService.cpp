#include "DiskBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace ArcMeta {

int DiskBatchRenameService::execute(const std::vector<std::wstring>& originalPaths,
                                    const std::vector<std::wstring>& newNames,
                                    DiskOperationMode mode,
                                    const QString& targetDir) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) return 0;

    int successCount = 0;
    MetadataManager::instance().setInternalOperating(true);

    for (size_t i = 0; i < originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(originalPaths[i]);
        QFileInfo oldInfo(oldPath);

        QString destDir = (mode == DiskOperationMode::Rename) ? oldInfo.absolutePath() : targetDir;
        QString newPathStr = QDir(destDir).filePath(QString::fromStdWString(newNames[i]));

        bool ok = false;
        // 处理旁路缩略图 <basename>_thumbnail.png
        QString oldThumbPath = oldInfo.absolutePath() + "/" + oldInfo.completeBaseName() + "_thumbnail.png";
        QString newThumbPath = QFileInfo(newPathStr).absolutePath() + "/" + QFileInfo(newPathStr).completeBaseName() + "_thumbnail.png";

        if (mode == DiskOperationMode::Copy) {
            ok = QFile::copy(oldPath, newPathStr);
            if (ok && QFile::exists(oldThumbPath)) {
                QFile::copy(oldThumbPath, newThumbPath);
            }
        } else if (mode == DiskOperationMode::Move) {
            if (QFile::copy(oldPath, newPathStr)) {
                ok = QFile::remove(oldPath);
                if (ok && QFile::exists(oldThumbPath)) {
                    if (QFile::copy(oldThumbPath, newThumbPath)) {
                        QFile::remove(oldThumbPath);
                    }
                }
            }
        } else { // Rename
            ok = QFile::rename(oldPath, newPathStr);
            if (ok && QFile::exists(oldThumbPath)) {
                QFile::rename(oldThumbPath, newThumbPath);
            }
        }

        if (ok) {
            successCount++;
            if (mode != DiskOperationMode::Copy) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir(destDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();

                MetadataManager::instance().renameItem(oldW, newW);
                CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
            }
        }
    }

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();

    return successCount;
}

} // namespace ArcMeta
