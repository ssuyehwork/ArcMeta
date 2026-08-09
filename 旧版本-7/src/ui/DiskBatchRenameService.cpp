#include "DiskBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/CapsuleMediaExtractor.h"
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
        if (mode == DiskOperationMode::Copy) {
            ok = QFile::copy(oldPath, newPathStr);
        } else if (mode == DiskOperationMode::Move) {
            if (QFile::copy(oldPath, newPathStr)) {
                ok = QFile::remove(oldPath);
            }
        } else { // Rename
            ok = QFile::rename(oldPath, newPathStr);
        }

        if (ok) {
            successCount++;

            // 同步对 .arcmeta/disk_thumbs/ 中的哈希缩略图进行重命名/迁移/复制
            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPath);
            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);

            if (QFile::exists(oldThumbHashPath)) {
                if (mode == DiskOperationMode::Copy) {
                    QFile::copy(oldThumbHashPath, newThumbHashPath);
                } else { // Rename 或 Move
                    QFile::rename(oldThumbHashPath, newThumbHashPath);
                }
            }

            if (mode != DiskOperationMode::Copy) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir(destDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();

                // 调用 renameItemSync 同步机制
                MetadataManager::instance().renameItemSync(oldW, newW);
                CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
            }
        }
    }

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();

    return successCount;
}

} // namespace ArcMeta
