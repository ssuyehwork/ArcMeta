#pragma once

#include <string>
#include <vector>

namespace ArcMeta {

struct Favorite {
    std::wstring path;
    std::wstring type;
    std::wstring name;
    int sortOrder = 0;
};

/**
 * @brief 收藏夹持久层
 * 彻底废除数据库，全量转向 SCCH 架构
 */
class FavoritesRepo {
public:
    static bool add(const Favorite& fav);
    static bool remove(const std::wstring& path);
    static std::vector<Favorite> getAll();
};

} // namespace ArcMeta
