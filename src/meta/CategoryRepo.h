#pragma once

#include <string>
#include <vector>
#include <QString>
#include <QMap>
#include <atomic>
#include <functional>
#include <map>
#include "sqlite3.h"

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

struct CategoryItem {
    std::string fileId128;
    std::wstring pathHint;
};

class CategoryRepo {
public:
    static constexpr int TRASH_CATEGORY_ID    = -8;
    static constexpr int UNCATEGORIZED_CAT_ID = -2;

    static std::wstring getDefaultColor() { return L"#555555"; }

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

    static bool addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint = L"");
    static bool removeItemFromCategory(int categoryId, const std::string& fileId128);
    static bool removeAllCategories(const std::string& fileId128);
    static bool removeAllCategoriesBatch(const std::vector<std::string>& fids);
    static std::vector<int> getItemCategoryIds(const std::string& fid);
    static bool moveToTrashBatch(const std::vector<std::string>& fids);

    static bool restoreFromTrash(const std::string& fileId128);
    static bool restoreFromTrashBatch(const std::vector<std::string>& fids);
    static bool permanentlyDelete(const std::string& fileId128);
    static bool permanentlyDeleteBatch(const std::vector<std::string>& fids);

    static std::vector<CategoryItem> getItemsInCategory(int categoryId);
    static std::vector<CategoryItem> getItemsInCategories(const std::vector<int>& categoryIds);
    static std::vector<CategoryItem> getItemsRecursive(int categoryId);
    static std::vector<int> getSubtreeIds(int categoryId);

    static std::vector<std::string> getFileIdsInCategory(int categoryId);
    static std::vector<std::string> getFileIdsRecursive(int categoryId);

    static void saveImmediately();
    static void initialize();

    static int getTotalFileCount();
    static int getUncategorizedCount();
    static void setTotalFileCount(int count);
    static void setCategorizedCount(int count);
    static void incrementTotalFileCount(int delta);
    static void incrementCategorizedCount(int delta);
    
    static void fullRecount();
    static void updatePersistentStat(const std::string& key, int delta);
    static bool executeFidBatch(const std::vector<std::string>& fids, std::function<bool(sqlite3*, const std::string&)> action);
    static void syncCategorizedCountForFid(const std::string& fid);

    static constexpr const char* STAT_TOTAL_FILES = "total_file_count";
    static constexpr const char* STAT_CATEGORIZED = "categorized_count";

    static std::atomic<int> s_totalFileCount;
    static std::atomic<int> s_categorizedCount;
};

} // namespace ArcMeta
