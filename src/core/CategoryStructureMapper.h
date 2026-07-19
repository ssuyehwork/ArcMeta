#ifndef ARCMETA_CATEGORY_STRUCTURE_MAPPER_H
#define ARCMETA_CATEGORY_STRUCTURE_MAPPER_H

#include <string>

namespace ArcMeta {

class CategoryStructureMapper {
public:
    /**
     * @brief 自动建立 1:1 分级镜像目录树逻辑
     * @param rootPath 物理绝对路径
     * @param outCatId 分类建立完成后的叶子节点 ID
     */
    static bool ensureCategoryPath(const std::wstring& rootPath, int& outCatId);
};

} // namespace ArcMeta

#endif // ARCMETA_CATEGORY_STRUCTURE_MAPPER_H
