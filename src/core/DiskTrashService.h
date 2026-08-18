#pragma once

#include <QString>
#include <QStringList>
#include <QObject>

namespace QuarkMeta {

/**
 * @brief 磁盘导航模式下的物理回收站服务层 (DiskTrashService)
 * 专门接管磁盘模式下的删除、还原与抹除，实现双轨物理隔离。
 */
class DiskTrashService : public QObject {
    Q_OBJECT
public:
    /**
     * @brief 将磁盘文件/文件夹移入磁盘回收站暂存区
     */
    static bool moveToDiskTrash(const QStringList& paths);

    /**
     * @brief 从暂存区还原指定的磁盘项目
     */
    static bool restoreFromDiskTrash(int id, const QString& trashPath);

    /**
     * @brief 彻底物理删除/抹除暂存区项目
     */
    static bool permanentlyDeleteDiskTrash(int id, const QString& trashPath);

    /**
     * @brief 还原所有磁盘回收站中的物理文件
     */
    static bool restoreAllDiskTrash();

    /**
     * @brief 彻底清空所有磁盘回收站中的物理文件
     */
    static bool emptyDiskTrash();
};

} // namespace QuarkMeta
