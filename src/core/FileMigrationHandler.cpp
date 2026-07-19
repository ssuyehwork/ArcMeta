#include "FileMigrationHandler.h"
#include "../util/ImportHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace ArcMeta {

bool FileMigrationHandler::checkMigrationAllowed(const QStringList& sourcePaths, const QString& destinationLibraryPath) {
    if (sourcePaths.isEmpty() || destinationLibraryPath.isEmpty()) return false;
    
    QDir destDir(destinationLibraryPath);
    if (!destDir.exists()) return false;

    for (const auto& path : sourcePaths) {
        QFileInfo info(path);
        if (!info.exists()) return false;

        // 禁止移动到其自身或其子目录
        if (destinationLibraryPath.startsWith(path, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

void FileMigrationHandler::executeMigration(const QStringList& sourcePaths, const QString& destinationLibraryPath) {
    if (!checkMigrationAllowed(sourcePaths, destinationLibraryPath)) {
        qWarning() << "[Migration] 迁移校验不通过，拒绝迁移。";
        return;
    }

    // 调用物理迁移助手，USN/MFT 自动记录捕获入库
    ImportHelper::importPaths(sourcePaths, destinationLibraryPath);
}

} // namespace ArcMeta
