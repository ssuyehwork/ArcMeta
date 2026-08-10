#pragma once
#include <QString>
#include <QFileInfo>
#include <string>
#include <QFile>
#include <QUuid>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

class FileOperationHelper {
public:
    /**
     * @brief 安全重命名（包含 Windows NTFS 大小写中转改名处理）
     */
    static bool safeRename(const QString& oldPath, const QString& newPath) {
        if (oldPath == newPath) return true;

        QFileInfo oldInfo(oldPath);
        QFileInfo newInfo(newPath);

        // 检测是否仅存在大小写差异
        bool isOnlyCaseChange = (oldInfo.absoluteFilePath().compare(
            newInfo.absoluteFilePath(), Qt::CaseInsensitive) == 0);

        if (isOnlyCaseChange) {
            // 第一阶段：重命名为中转 UUID 文件
            QString tempPath = oldPath + ".arc_tmp_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!QFile::rename(oldPath, tempPath)) {
                return false;
            }
            // 第二阶段：从中转文件重命名为目标路径
            if (!QFile::rename(tempPath, newPath)) {
                QFile::rename(tempPath, oldPath); // 失败回滚
                return false;
            }
            return true;
        }

        return QFile::rename(oldPath, newPath);
    }

    /**
     * @brief 安全原子移动（废除 copy + remove）
     */
    static bool safeMove(const QString& oldPath, const QString& newPath) {
#ifdef Q_OS_WIN
        std::wstring wOld = QDir::toNativeSeparators(oldPath).toStdWString();
        std::wstring wNew = QDir::toNativeSeparators(newPath).toStdWString();
        BOOL res = ::MoveFileExW(wOld.c_str(), wNew.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
        return (res != 0);
#else
        if (QFile::exists(newPath)) QFile::remove(newPath);
        return QFile::rename(oldPath, newPath);
#endif
    }
};

} // namespace ArcMeta
