#ifndef ARCMETA_HISTORY_PATH_MANAGER_H
#define ARCMETA_HISTORY_PATH_MANAGER_H

#include <string>
#include <QStringList>

namespace ArcMeta {

class HistoryPathManager {
public:
    /**
     * @brief 记录并记忆 14 条最近访问路径的活跃历史记录功能
     * @param path 物理绝对路径
     */
    static void recordRecentVisitedFolder(const std::wstring& path);

    /**
     * @brief 获取指定卷的最近访问路径列表
     * @param volSerial 磁盘卷序列号
     */
    static QStringList getRecentVisitedFolders(const std::wstring& volSerial);
};

} // namespace ArcMeta

#endif // ARCMETA_HISTORY_PATH_MANAGER_H
