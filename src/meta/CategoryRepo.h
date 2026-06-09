#pragma once

#include <string>
#include <vector>
#include <QString>
#include <QMap>
#include <atomic>

namespace ArcMeta {

struct Category {
    int id = 0;
    int parentId = 0;
    std::wstring name;
    std::wstring color;
    std::vector<std::wstring> presetTags;
    int sortOrder = 0;
    bool pinned = false;
    bool encrypted = false;
    std::wstring encryptHint;
};

/**
 * @brief 分类项记录（含路径提示）
 */
struct CategoryItem {
    std::string fileId128;
    std::wstring pathHint;
};

/**
 * @brief 分类持久层
 * 彻底废除数据库，全量转向 SCCH 架构
 */
class CategoryRepo {
public:
    static bool add(Category& cat);
    static bool update(const Category& cat);
    static int findCategoryId(int parentId, const std::wstring& name);
    static bool remove(int id);
    static bool reorder(int parentId, bool ascending);
    static bool reorderAll(bool ascending);
    static std::vector<Category> getAll();
    static std::vector<Category> getRecentlyUsed(int limit);
    static std::vector<std::pair<int, int>> getCounts();
    static int getUniqueItemCount();
    static int getUncategorizedItemCount();
    static QMap<QString, int> getSystemCounts();
    static QStringList getSystemCategoryPaths(const QString& type);

    // 条目关联逻辑
    static bool addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint = L"");
    static bool removeItemFromCategory(int categoryId, const std::string& fileId128);
    static bool removeAllCategories(const std::string& fileId128);
    static bool removeAllCategoriesBatch(const std::vector<std::string>& fids);
    static std::vector<CategoryItem> getItemsInCategory(int categoryId);
    static std::vector<CategoryItem> getItemsRecursive(int categoryId);

    // 废弃接口（保持兼容）
    static std::vector<std::string> getFileIdsInCategory(int categoryId);
    static std::vector<std::string> getFileIdsRecursive(int categoryId);

    static void saveImmediately();

    /**
     * @brief 2026-06-xx 物理修复：在主线程预热缓存管理器，确保定时器线程归属正确
     */
    static void initialize();

    // 增量计数接口 (Part 4)
    static int getTotalFileCount();
    static int getUncategorizedCount();
    static void setTotalFileCount(int count);
    static void setCategorizedCount(int count);
    static void incrementTotalFileCount(int delta);
    static void incrementCategorizedCount(int delta);
    
    /**
     * @brief 强制执行全量计数重计 (物理账本对账)
     */
    static void fullRecount();

    static std::atomic<int> s_totalFileCount;
    static std::atomic<int> s_categorizedCount;
};

} // namespace ArcMeta
