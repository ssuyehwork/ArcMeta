#pragma once

#include <string>
#include <vector>
#include <memory>
#include <QString>
#include <QMap>
#include <QSet>
#include <atomic>
#include <functional>
#include <mutex>
#include <map>
#include <unordered_map>
#include "sqlite3.h"

namespace ArcMeta {

enum class CategoryKind : int {
    User = 0,           // 用户自定义分类（默认）
    SystemLibrary = 1   // 系统托管库根分类
};

struct StatisticsSnapshot { 
    // 1. 静态分类计数 (key -> count) 
    // 包含: "all", "uncategorized", "untagged", "recently_visited", "tags", "trash" 
    QMap<QString, int> systemCounts; 
 
    // 2. 半静态托管库计数 (categoryId -> count) 
    // 键为托管库分类ID，值为对应盘中有效素材总数 
    QMap<int, int> libraryCounts; 
 
    // 3. 全动态用户分类计数 (categoryId -> count) 
    // 键为用户分类ID，值为关联的去重有效素材数 
    QMap<int, int> userCategoryCounts; 
}; 

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
    uint64_t physicalFrn = 0;
    std::wstring physicalPath;
    std::wstring icon = L"folder_filled";
    CategoryKind kind = CategoryKind::User; // 新增：0=User, 1=SystemLibrary
};

/**
 * @brief 分类项记录（含路径提示）
 */
struct CategoryItem {
    std::string folderId;
    std::wstring pathHint;
};

/**
 * @brief 分类持久层，基于中心化数据库实现
 */
class CategoryRepo {
public:
    // 2026-06-xx 物理同步：与 CategoryModel.cpp 定义的系统项 ID 保持绝对一致
    static constexpr int TRASH_CATEGORY_ID    = -8;
    static constexpr int UNCATEGORIZED_CAT_ID = -2;

    /**
     * @brief 获取默认分类颜色：深灰色 (#555555)
     */
    static std::wstring getDefaultColor() { return L"#555555"; }

    /**
     * @brief 自动将文件/胶囊绑定到其所在盘符的托管库根分类上
     * @param folderId 13位资产ID (00ms...)
     * @param physicalPath 文件绝对路径
     */
    static void bindToLibraryRootCategory(const std::string& folderId, const std::wstring& physicalPath);

    /**
     * @brief 唯一合法的统计函数，一次性产出标准化账本结构体 StatisticsSnapshot
     */
    static StatisticsSnapshot calculateAllStatistics();

    /**
     * @brief 唯一合法的异步统计接口
     */
    static void calculateAllStatisticsAsync(std::function<void(const StatisticsSnapshot&)> callback);

    static bool add(Category& cat);
    static bool update(const Category& cat);
    static Category getById(int id);
    static int findCategoryId(int parentId, const std::wstring& name);
    static int findByFrn(uint64_t frn);
    static bool updatePhysicalMapping(int id, uint64_t frn, const std::wstring& path);
    static bool remove(int id);
    static bool reorder(int parentId, bool ascending);
    static bool reorderAll(bool ascending);
    static std::vector<Category> getAll();
    static std::vector<Category> getRecentlyUsed(int limit);

    // 🚨 【核心重构】：UI 线程专属高并发无锁只读接口（0 毫秒 SQL 阻塞）
    static void refreshMemoryCache();
    static std::vector<Category> getCachedAll();
    static Category getCachedById(int id);
    static std::vector<Category> getCachedRecentlyUsed(size_t limit = 15);

    static std::vector<std::pair<int, int>> getCounts();
    static int getUniqueItemCount();
    static int getUncategorizedItemCount();
    static QMap<QString, int> getRawSystemCounts();
    static QMap<QString, int> getSystemCounts();
    static QStringList getSystemCategoryPaths(const QString& type);

    /** 
     * @brief 获取全局所有唯一标签及其使用频次映射 
     * @return QMap<标签名, 使用次数> 
     */ 
    static QMap<QString, int> getGlobalUniqueTags();

    // 条目关联逻辑
    static bool updateCategoryColorByPath(const std::wstring& path, const std::wstring& color);
    static bool renamePhysicalCategoryPath(const std::wstring& oldPath, const std::wstring& newPath);
    static bool addItemToCategory(int categoryId, const std::string& folderId, const std::wstring& pathHint = L"");
    static bool addItemToCategoryBatch(int categoryId, const std::vector<std::pair<std::string, std::wstring>>& items);
    static bool removeItemFromCategory(int categoryId, const std::string& folderId);
    static bool removeAllCategories(const std::string& folderId);
    static bool removeAllCategoriesBatch(const std::vector<std::string>& folderIds);
    static std::vector<int> getItemCategoryIds(const std::string& folderId, const std::wstring& pathHint = L"");
    static bool moveToTrashBatch(const std::vector<std::string>& folderIds);

    static bool restoreFromTrash(const std::string& folderId);
    static bool restoreFromTrashBatch(const std::vector<std::string>& folderIds);
    static bool permanentlyDelete(const std::string& folderId);
    static bool permanentlyDeleteBatch(const std::vector<std::string>& folderIds);

    static std::vector<CategoryItem> getItemsInCategory(int categoryId);
    static std::vector<CategoryItem> getItemsInCategories(const std::vector<int>& categoryIds);
    static std::vector<CategoryItem> getItemsRecursive(int categoryId);
    static std::vector<int> getSubtreeIds(int categoryId);

    // 废弃接口（保持兼容）
    static std::vector<std::string> getFolderIdsInCategory(int categoryId);
    static std::vector<std::string> getFolderIdsRecursive(int categoryId);

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
     * @brief 从数据库加载历史持久化计数
     */
    static void loadStatsFromDb();

    /**
     * @brief 强制执行全量计数重计 (物理账本对账)
     */
    static void fullRecount();

    /**
     * @brief 异步强制执行全量计数重计 (物理账本对账)
     */
    static void fullRecountAsync(std::function<void(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts)> callback = nullptr);

    /**
     * @brief 统计指标更新原子化
     * @param key 统计项 Key (total_file_count, categorized_count)
     * @param delta 增量
     */
    static void updatePersistentStat(const std::string& key, int delta);

    /**
     * @brief 批量执行 FID 处理任务
     */
    static bool executeFidBatch(const std::vector<std::string>& fids, std::function<bool(sqlite3*, const std::string&)> action);

    /**
     * @brief 自动同步已分类计数
     */
    static void syncCategorizedCountForFid(const std::string& fid);

    // 2026-06-xx 逻辑收拢：系统指标键名常量
    static constexpr const char* STAT_TOTAL_FILES = "total_file_count";
    static constexpr const char* STAT_CATEGORIZED = "categorized_count";

    static std::atomic<int> s_totalFileCount;
    static std::atomic<int> s_categorizedCount;
    static std::atomic<bool> s_countsDirty;

    // 2026-08-xx：侧边栏高性能原子计数器
    static std::atomic<int> s_totalCount;             // 对应 "all"
    static std::atomic<int> s_tagsCount;              // 对应 "tags"
    static std::atomic<int> s_recentlyVisitedCount;   // 对应 "recently_visited"
    static std::atomic<int> s_untaggedCount;          // 对应 "untagged"
    static std::atomic<int> s_uncategorizedCount;     // 对应 "uncategorized"
    static std::atomic<int> s_trashCount;             // 对应 "trash"

    static std::mutex s_tagsMutex;
    static QSet<QString> s_globalTagsSet;

private:
    // 内存快照只读指针（RCU Lock-Free 机制）
    static std::shared_ptr<const std::vector<Category>> s_categoryCache;
    static std::shared_ptr<const std::vector<Category>> s_recentlyUsedCache;
    static std::shared_ptr<const std::unordered_map<std::string, std::vector<int>>> s_itemCategoriesCache;
    static std::mutex s_cacheMutex;
};

} // namespace ArcMeta
