#include "ShellHelper.h"
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"

namespace ArcMeta {

bool ShellHelper::moveToTrash(const QStringList& paths) {
    if (paths.isEmpty()) return true;
    
    bool allOk = true;
    for (const QString& p : paths) {
        QFileInfo info(p);
        QString drive = info.absolutePath().left(3); // e.g. "C:/"
        QString trashDir = drive + ".arcmeta/trash";
        QDir().mkpath(trashDir);
        
#ifdef Q_OS_WIN
        // 确保 .arcmeta 目录隐藏
        SetFileAttributesW((drive + ".arcmeta").toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

        QString dest = trashDir + "/" + info.fileName();
        // 冲突处理：如果回收站已有同名文件，增加时间戳后缀
        if (QFile::exists(dest)) {
            dest = trashDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + info.fileName();
        }

        // 1. 物理移动
        if (QFile::rename(p, dest)) {
            // 2. 数据库同步：标记为回收站，记忆原路径
            MetadataManager::instance().markAsTrash(dest.toStdWString(), true, p.toStdWString());
            // 3. 解除所有分类关联
            std::string fid = MetadataManager::instance().getFileIdSync(dest.toStdWString());
            CategoryRepo::removeAllCategories(fid);
        } else {
            allOk = false;
        }
    }
    return allOk;
}

bool ShellHelper::copyOrMoveItems(const QStringList& sourcePaths, const QString& destDir, bool isMove) {
#ifdef Q_OS_WIN
    if (sourcePaths.isEmpty() || destDir.isEmpty()) return false;
    
    std::wstring from;
    for (const QString& p : sourcePaths) {
        from += QDir::toNativeSeparators(p).toStdWString() + L'\0';
    }
    from += L'\0';

    std::wstring to = QDir::toNativeSeparators(destDir).toStdWString() + L'\0' + L'\0';

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.wFunc = isMove ? FO_MOVE : FO_COPY;
    fileOp.pFrom = from.c_str();
    fileOp.pTo = to.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR;
    bool ok = (SHFileOperationW(&fileOp) == 0);
    if (ok && isMove) {
        for (const QString& p : sourcePaths) {
            QFileInfo info(p);
            QString newPath = QDir(destDir).filePath(info.fileName());
            MetadataManager::instance().renameItem(p.toStdWString(), newPath.toStdWString());
        }
    }
    return ok;
#else
    Q_UNUSED(sourcePaths);
    Q_UNUSED(destDir);
    Q_UNUSED(isMove);
    return false;
#endif
}

void ShellHelper::showProperties(const QString& path) {
#ifdef Q_OS_WIN
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = L"properties";
    std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    sei.lpFile = wpath.c_str();
    sei.nShow = SW_SHOW;
    ShellExecuteExW(&sei);
#else
    Q_UNUSED(path);
#endif
}

void ShellHelper::openInExplorer(const QString& path) {
#ifdef Q_OS_WIN
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(path);
    QProcess::startDetached("explorer", args);
#else
    Q_UNUSED(path);
#endif
}

bool ShellHelper::renameItem(const QString& oldPath, const QString& newPath) {
    if (QFile::rename(oldPath, newPath)) {
        // 同步数据库
        MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
        return true;
    }
    return false;
}

QString ShellHelper::formatSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    if (bytes < 1024LL * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

void ShellHelper::ensureHidden(const std::wstring& path) {
#ifdef Q_OS_WIN
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
#else
    Q_UNUSED(path);
#endif
}

QString ShellHelper::resolveAndAlignDatabasePath(const std::wstring& volumeSerial, const QString& driveLetter, const QString& currentDiskPathInConn, bool isLoaded) {
    QString cleanLetter = "";
    if (!driveLetter.isEmpty()) {
        cleanLetter = driveLetter.at(0).toUpper();
    }

    QString appDir = QCoreApplication::applicationDirPath();
    QString metaDir = appDir + "/.arcmeta";
    QDir().mkpath(metaDir);
    ensureHidden(metaDir.toStdWString());

    QString serialStr = QString::fromStdWString(volumeSerial).toUpper();
    QString expectedFileName = QString("Arcmeta_%1%2.db").arg(serialStr).arg(cleanLetter.isEmpty() ? "" : "_" + cleanLetter);
    QString targetPath = metaDir + "/" + expectedFileName;

    if (isLoaded) {
        if (!cleanLetter.isEmpty()) {
            if (!currentDiskPathInConn.endsWith(expectedFileName)) {
                qDebug() << "[ShellHelper] 检测到盘符漂移，执行物理纠偏重命名:" << currentDiskPathInConn << " -> " << targetPath;
                
                // 如果目标已存在且不是自己，先将其移走（按用户规则重命名为无效）
                if (QFile::exists(targetPath) && targetPath != currentDiskPathInConn) {
                    QString invalidBase = QString("%1/Arcmeta_%2_无效").arg(metaDir).arg(serialStr);
                    QString invalidPath = invalidBase + ".db";
                    int counter = 1;
                    while (QFile::exists(invalidPath)) {
                        invalidPath = QString("%1_%2.db").arg(invalidBase).arg(counter++);
                    }
                    qDebug() << "[ShellHelper] 目标文件已存在，先将其重命名为无效:" << invalidPath;
                    QFile::rename(targetPath, invalidPath);
                }

                if (QFile::rename(currentDiskPathInConn, targetPath)) {
                    qDebug() << "[ShellHelper] 物理重命名成功";
                    return targetPath;
                } else {
                    qWarning() << "[ShellHelper] 物理重命名失败";
                }
            }
        }
        return currentDiskPathInConn;
    }

    // 未加载时的路由/纠偏
    if (!QFile::exists(targetPath)) {
        QDir dir(metaDir);
        QStringList filters;
        filters << QString("Arcmeta_%1*.db").arg(serialStr);
        QFileInfoList list = dir.entryInfoList(filters, QDir::Files | QDir::Hidden | QDir::System, QDir::Time);

        if (!list.isEmpty()) {
            // Case A: 有旧文件。选择最近修改的一个作为目标进行重命名。
            QFileInfo bestInfo = list.first();
            if (!cleanLetter.isEmpty()) {
                if (QFile::rename(bestInfo.absoluteFilePath(), targetPath)) {
                    qDebug() << "[ShellHelper] 自动纠偏：重命名数据库" << bestInfo.fileName() << "->" << expectedFileName;
                } else {
                    qWarning() << "[ShellHelper] 重命名失败，降级使用原文件加载:" << bestInfo.absoluteFilePath();
                    targetPath = bestInfo.absoluteFilePath();
                }
            } else {
                targetPath = bestInfo.absoluteFilePath();
            }

            // 处理冲突的其他旧文件 (Plan-97 补充要求)
            for (int i = 1; i < list.size(); ++i) {
                QString conflictPath = list.at(i).absoluteFilePath();
                QString invalidBase = QString("%1/Arcmeta_%2_无效").arg(metaDir).arg(serialStr);
                QString invalidPath = invalidBase + ".db";
                int counter = 1;
                while (QFile::exists(invalidPath)) {
                    invalidPath = QString("%1_%2.db").arg(invalidBase).arg(counter++);
                }
                if (QFile::rename(conflictPath, invalidPath)) {
                    qDebug() << "[ShellHelper] 冲突处理：将冗余数据库标记为无效" << list.at(i).fileName() << "->" << QFileInfo(invalidPath).fileName();
                }
            }
        }
    }

    return targetPath;
}

} // namespace ArcMeta
