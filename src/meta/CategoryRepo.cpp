#include "CategoryRepo.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include "MetadataManager.h"

namespace ArcMeta {

namespace ScchCategoryEngine {

// 2026-06-xx 二进制分类文件头
struct CategoryHeader {
    char magic[4] = {'C', 'A', 'T', 'S'};
    uint32_t version = 3;
    uint32_t catCount = 0;
    uint32_t itemCount = 0;
};

static QDataStream& operator<<(QDataStream& ds, const std::string& s) {
    ds << QString::fromStdString(s);
    return ds;
}
static QDataStream& operator>>(QDataStream& ds, std::string& s) {
    QString qs; ds >> qs; s = qs.toStdString();
    return ds;
}

static QDataStream& operator<<(QDataStream& ds, const Category& c) {
    ds << c.id << c.parentId << QString::fromStdWString(c.name) << QString::fromStdWString(c.color);
    ds << (int)c.presetTags.size();
    for (const auto& t : c.presetTags) ds << QString::fromStdWString(t);
    ds << c.sortOrder << c.pinned << c.encrypted << QString::fromStdWString(c.encryptHint);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, Category& c) {
    QString name, color, hint;
    ds >> c.id >> c.parentId >> name >> color;
    c.name = name.toStdWString(); c.color = color.toStdWString();
    int tagCount; ds >> tagCount;
    c.presetTags.clear();
    for (int i = 0; i < tagCount; ++i) { QString t; ds >> t; c.presetTags.push_back(t.toStdWString()); }
    ds >> c.sortOrder >> c.pinned >> c.encrypted >> hint;
    c.encryptHint = hint.toStdWString();
    return ds;
}

struct CategoryItemRecord {
    int categoryId;
    std::string fileId128;
    double addedAt;
};

static QDataStream& operator<<(QDataStream& ds, const CategoryItemRecord& r) {
    ds << r.categoryId << QString::fromStdString(r.fileId128) << r.addedAt;
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, CategoryItemRecord& r) {
    QString fid;
    ds >> r.categoryId >> fid >> r.addedAt;
    r.fileId128 = fid.toStdString();
    return ds;
}

static bool loadAll(std::vector<Category>& cats, std::vector<CategoryItemRecord>& items) {
    QFile file("arcmeta_categories.scch");
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return false;
    
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    CategoryHeader header;
    file.read((char*)&header, sizeof(header));
    if (memcmp(header.magic, "CATS", 4) != 0) return false;

    cats.clear();
    for (uint32_t i = 0; i < header.catCount; ++i) { Category c; ds >> c; cats.push_back(c); }
    items.clear();
    for (uint32_t i = 0; i < header.itemCount; ++i) { CategoryItemRecord r; ds >> r; items.push_back(r); }
    return true;
}

static bool saveAll(const std::vector<Category>& cats, const std::vector<CategoryItemRecord>& items) {
    QFile file("arcmeta_categories.scch");
    if (!file.open(QIODevice::WriteOnly)) return false;
    
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    CategoryHeader header;
    header.catCount = (uint32_t)cats.size();
    header.itemCount = (uint32_t)items.size();
    file.write((char*)&header, sizeof(header));

    for (const auto& c : cats) ds << c;
    for (const auto& r : items) ds << r;
    return true;
}

std::vector<Category> getAll() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::sort(cats.begin(), cats.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });
    return cats;
}

bool add(Category& cat) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    int maxId = 0;
    for (const auto& c : cats) if (c.id > maxId) maxId = c.id;
    cat.id = maxId + 1;
    cats.push_back(cat);
    return saveAll(cats, items);
}

bool update(const Category& cat) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    bool found = false;
    for (auto& c : cats) {
        if (c.id == cat.id) { c = cat; found = true; break; }
    }
    if (!found) cats.push_back(cat);
    return saveAll(cats, items);
}

static void collectSubIds(const std::vector<Category>& all, int pid, std::vector<int>& ids) {
    for (const auto& c : all) {
        if (c.parentId == pid) { ids.push_back(c.id); collectSubIds(all, c.id, ids); }
    }
}

bool remove(int id) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::vector<int> removeIds = {id};
    collectSubIds(cats, id, removeIds);

    cats.erase(std::remove_if(cats.begin(), cats.end(), [&](const Category& c) {
        return std::find(removeIds.begin(), removeIds.end(), c.id) != removeIds.end();
    }), cats.end());

    items.erase(std::remove_if(items.begin(), items.end(), [&](const CategoryItemRecord& r) {
        return std::find(removeIds.begin(), removeIds.end(), r.categoryId) != removeIds.end();
    }), items.end());

    return saveAll(cats, items);
}

bool reorder(int parentId, bool ascending) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::vector<Category*> targets;
    for (auto& c : cats) if (c.parentId == parentId) targets.push_back(&c);
    
    std::sort(targets.begin(), targets.end(), [ascending](Category* a, Category* b) {
        int cmp = a->name.compare(b->name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    for (size_t i = 0; i < targets.size(); ++i) targets[i]->sortOrder = (int)i;
    return saveAll(cats, items);
}

bool reorderAll(bool ascending) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::sort(cats.begin(), cats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });
    for (size_t i = 0; i < cats.size(); ++i) cats[i].sortOrder = (int)i;
    return saveAll(cats, items);
}

bool addItemToCategory(int categoryId, const std::string& fileId128) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    for (const auto& r : items) if (r.categoryId == categoryId && r.fileId128 == fileId128) return true;
    items.push_back({categoryId, fileId128, (double)QDateTime::currentMSecsSinceEpoch()});
    return saveAll(cats, items);
}

bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    items.erase(std::remove_if(items.begin(), items.end(), [&](const CategoryItemRecord& r) {
        return r.categoryId == categoryId && r.fileId128 == fileId128;
    }), items.end());
    return saveAll(cats, items);
}

std::vector<std::string> getFileIdsInCategory(int categoryId) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    // 2026-06-xx 按照用户要求：仅获取文件 ID，排除文件夹
    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::vector<std::string> results;
    for (const auto& r : items) {
        if (r.categoryId == categoryId && folderIds.find(r.fileId128) == folderIds.end()) {
            results.push_back(r.fileId128);
        }
    }
    return results;
}

std::vector<std::pair<int, int>> getCounts() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    loadAll(cats, records);

    // 2026-06-xx 物理同步：获取所有已知的文件夹 ID，用于排除统计
    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    // 2026-06-xx 物理同步：按分类 ID 分组，使用 set 对 file_id_128 进行去重统计，并排除文件夹
    QMap<int, std::unordered_set<std::string>> catFileSets;
    for (const auto& r : records) {
        if (!r.fileId128.empty() && folderIds.find(r.fileId128) == folderIds.end()) {
            catFileSets[r.categoryId].insert(r.fileId128);
        }
    }

    std::vector<std::pair<int, int>> res;
    for (auto it = catFileSets.begin(); it != catFileSets.end(); ++it) {
        res.push_back({it.key(), (int)it.value().size()});
    }
    return res;
}

std::vector<std::string> getFileIdsRecursive(int categoryId) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::vector<int> ids = {categoryId};
    collectSubIds(cats, categoryId, ids);
    std::vector<std::string> results;
    for (const auto& r : items) {
        if (std::find(ids.begin(), ids.end(), r.categoryId) != ids.end() &&
            folderIds.find(r.fileId128) == folderIds.end()) {
            results.push_back(r.fileId128);
        }
    }
    return results;
}

} // namespace ScchCategoryEngine

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) { (void)limit; return ScchCategoryEngine::getAll(); }
bool CategoryRepo::add(Category& cat) { return ScchCategoryEngine::add(cat); }
bool CategoryRepo::reorderAll(bool ascending) { return ScchCategoryEngine::reorderAll(ascending); }
bool CategoryRepo::update(const Category& cat) { return ScchCategoryEngine::update(cat); }
bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128) { return ScchCategoryEngine::addItemToCategory(categoryId, fileId128); }
std::vector<Category> CategoryRepo::getAll() { return ScchCategoryEngine::getAll(); }
bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) { return ScchCategoryEngine::removeItemFromCategory(categoryId, fileId128); }
std::vector<std::string> CategoryRepo::getFileIdsInCategory(int categoryId) { return ScchCategoryEngine::getFileIdsInCategory(categoryId); }
std::vector<std::pair<int, int>> CategoryRepo::getCounts() { return ScchCategoryEngine::getCounts(); }
int CategoryRepo::getUniqueItemCount() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    ScchCategoryEngine::loadAll(cats, records);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::unordered_set<std::string> uniqueIds;
    for (const auto& r : records) {
        if (!r.fileId128.empty() && folderIds.find(r.fileId128) == folderIds.end()) {
            uniqueIds.insert(r.fileId128);
        }
    }
    return (int)uniqueIds.size();
}

int CategoryRepo::getUncategorizedItemCount() {
    // 1. 收集所有已分类的 file_id_128
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    ScchCategoryEngine::loadAll(cats, records);
    std::unordered_set<std::string> categorizedIds;
    for (const auto& r : records) {
        if (!r.fileId128.empty()) categorizedIds.insert(r.fileId128);
    }

    // 2. 遍历 MetadataManager 缓存，统计不在已分类集合中的“文件”
    int count = 0;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (!meta.isFolder && categorizedIds.find(meta.fileId128) == categorizedIds.end()) {
            count++;
        }
    });
    return count;
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> counts;
    int all = 0, today = 0, yesterday = 0, recentlyVisited = 0, untagged = 0;

    double now = (double)QDateTime::currentMSecsSinceEpoch();
    double startOfToday = (double)QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    double startOfYesterday = startOfToday - 86400000.0;

    // 2026-06-xx 物理同步：基于 MetadataManager 内存缓存进行纯文件模式统计
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.isFolder) return; // 按照用户要求：仅统计文件数量，排除文件夹

        all++;
        if (meta.tags.isEmpty()) untagged++;

        // 2026-06-xx 物理对账：利用补全的时间戳字段执行精准系统分类统计
        if (meta.ctime >= startOfToday || meta.mtime >= startOfToday) today++;
        if ((meta.ctime >= startOfYesterday || meta.mtime >= startOfYesterday) &&
            (meta.ctime < startOfToday && meta.mtime < startOfToday)) yesterday++;
        if (meta.atime >= now - 86400000.0) recentlyVisited++;
    });

    counts["all"] = all;
    counts["today"] = today;
    counts["yesterday"] = yesterday;
    counts["recently_visited"] = recentlyVisited;
    counts["untagged"] = untagged;
    counts["uncategorized"] = getUncategorizedItemCount();
    counts["trash"] = 0;
    return counts;
}
bool CategoryRepo::remove(int id) { return ScchCategoryEngine::remove(id); }
bool CategoryRepo::reorder(int parentId, bool ascending) { return ScchCategoryEngine::reorder(parentId, ascending); }
std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) { return ScchCategoryEngine::getFileIdsRecursive(categoryId); }

} // namespace ArcMeta
