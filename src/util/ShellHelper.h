#pragma once
#include <QString>
#include <QStringList>

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
    static bool moveToTrash(const QStringList& paths);

    /**
     * @brief 执行复制或移动
     */
    static bool copyOrMoveItems(const QStringList& sourcePaths, const QString& destDir, bool isMove);

    /**
     * @brief 显示文件属性对话框
     */
    static void showProperties(const QString& path);

    /**
     * @brief 在资源管理器中定位
     */
    static void openInExplorer(const QString& path);

    /**
     * @brief 重命名条目
     */
    static bool renameItem(const QString& oldPath, const QString& newPath);

    /**
     * @brief 格式化字节大小
     */
    static QString formatSize(qint64 bytes);

    /**
     * @brief 物理设置文件/文件夹隐藏属性 (解耦自 DatabaseManager)
     */
    static void ensureHidden(const std::wstring& path);

    /**
     * @brief 盘符漂移与冗余数据库物理纠偏路由 (解耦自 DatabaseManager)
     */
    static QString resolveAndAlignDatabasePath(const std::wstring& volumeSerial, const QString& driveLetter, const QString& currentDiskPathInConn = "", bool isLoaded = false);
};

} // namespace ArcMeta
