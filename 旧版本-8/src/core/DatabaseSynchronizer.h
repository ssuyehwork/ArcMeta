#pragma once

#include <string>

namespace ArcMeta {

/**
 * @brief 级联同步物理磁盘目录结构与数据库逻辑分类
 *
 * 将磁盘 DFS 扫描与对账等重型 I/O 从逻辑仓储 CategoryRepo 剥离至此服务中，保持高内聚。
 */
class DatabaseSynchronizer {
public:
    static void syncPhysicalDirectoryCascade(const std::wstring& rootPath);
};

} // namespace ArcMeta
