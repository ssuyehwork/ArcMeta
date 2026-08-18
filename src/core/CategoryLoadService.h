#pragma once

#include <QString>
#include <QStringList>
#include <vector>
#include "ItemRecord.h"

namespace QuarkMeta {

/**
 * @brief 【物理隔离模块二】资源库与内存分类加载服务
 *
 * 只从 SQLite 数据库和内存缓存读取数据，专责解包 .arc 资产包、
 * 展示内嵌主素材名、处理分类归属关系——与 DiskScanService 物理互不知晓。
 */
class CategoryLoadService {
public:
    /**
     * @brief 加载指定分类 ID 下的子分类与资产条目
     * @param categoryId 目标分类 ID
     * @param recursive 是否递归包含子分类下的资产
     */
    static std::vector<ItemRecord> loadCategoryItems(int categoryId, bool recursive);

    /**
     * @brief 按物理路径列表批量创建条目（用于系统分类/搜索结果/资源库内部路径等场景）
     */
    static std::vector<ItemRecord> loadPathItems(const QStringList& paths);

    /**
     * @brief 加载统一回收站中的所有项（包含“资源库-托管资产”与“目录导航-物理文件”）
     */
    static std::vector<ItemRecord> loadTrashItems();

    /**
     * @brief 🚨【安全过滤拦截】：判断指定资产所属分类是否处于未解锁的加锁状态
     */
    static bool isAssetLocked(const std::string& assetId);
};

} // namespace QuarkMeta
