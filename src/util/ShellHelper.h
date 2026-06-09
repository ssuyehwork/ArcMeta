#pragma once
#include <QString>
#include <QStringList>
#include <QDir>
#include <QProcess>
#include <vector>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace ArcMeta {

/**
 * @brief 系统服务层工具类 (ShellHelper)
 * 封装 Windows 原生 Shell 调用与通用格式化逻辑。
 */
class ShellHelper {
public:
    /**
     * @brief 移入回收站
     */
    static bool moveToTrash(const QStringList& paths) {
        if (paths.isEmpty()) return true;
        
        bool allOk = true;
        for (const QString& p : paths) {
            QFileInfo info(p);
            QString drive = info.absolutePath().left(3); // e.g. "C:/"
            QString trashDir = drive + ".arcmeta/trash";
            QDir().mkpath(trashDir);
            
            // 确保 .arcmeta 目录隐藏
            SetFileAttributesW((drive + ".arcmeta").toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);

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

    /**
     * @brief 执行复制或移动
     */
    static bool copyOrMoveItems(const QStringList& sourcePaths, const QString& destDir, bool isMove) {
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
        return SHFileOperationW(&fileOp) == 0;
#else
        return false;
#endif
    }

    /**
     * @brief 显示文件属性对话框
     */
    static void showProperties(const QString& path) {
#ifdef Q_OS_WIN
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
#endif
    }

    /**
     * @brief 在资源管理器中定位
     */
    static void openInExplorer(const QString& path) {
#ifdef Q_OS_WIN
        QStringList args;
        args << "/select," << QDir::toNativeSeparators(path);
        QProcess::startDetached("explorer", args);
#endif
    }

    /**
     * @brief 格式化字节大小
     */
    static QString formatSize(qint64 bytes) {
        if (bytes < 1024) return QString("%1 B").arg(bytes);
        if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
        if (bytes < 1024LL * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
};

} // namespace ArcMeta
