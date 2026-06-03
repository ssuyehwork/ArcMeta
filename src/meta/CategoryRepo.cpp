#include "CategoryRepo.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <unordered_set>
#include "MetadataManager.h"

namespace ArcMeta {

/**
 * @brief 内部记录结构：提升至顶级命名空间，确保跨作用域可见性
 */
struct CategoryItemRecord {
    int categoryId;
    std::string fileId128;
    double addedAt;

    CategoryItemRecord() : categoryId(0), addedAt(0.0) {}
    CategoryItemRecord(int catId, const std::string& fid, double time)
        : categoryId(catId), fileId128(fid), addedAt(time) {}
};

namespace ScchCategoryEngine {

/**
 * @brief 二进制分类文件头
 */
struct CategoryHeader {
    char magic[4];
    uint32_t version;
    uint32_t catCount;
    uint32_t itemCount;

    CategoryHeader() : version(3), catCount(0), itemCount(0) {
        magic[0] = 'C'; magic[1] = 'A'; magic[2] = 'T'; magic[3] = 'S';
    }
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
    ds << static_cast<int>(c.presetTags.size());
    for (size_t i = 0; i < c.presetTags.size(); ++i) ds << QString::fromStdWString(c.presetTags[i]);
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
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return false;
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
    header.catCount = static_cast<uint32_t>(cats.size());
    header.itemCount = static_cast<uint32_t>(items.size());
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (size_t i = 0; i < cats.size(); ++i) ds << cats[i];
    for (size_t i = 0; i < items.size(); ++i) ds << items[i];
    return true;
}

std::vector<Category> getAll() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    struct Sorter {
        bool operator()(const Category& a, const Category& b) const {
            return a.sortOrder < b.sortOrder;
        }
    } sorter;
    std::sort(cats.begin(), cats.end(), sorter);
    return cats;
}

bool add(Category& cat) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    int maxId = 0;
    for (size_t i = 0; i < cats.size(); ++i) if (cats[i].id > maxId) maxId = cats[i].id;
    cat.id = maxId + 1;
    cats.push_back(cat);
    return saveAll(cats, items);
}

bool update(const Category& cat) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    bool found = false;
    for (size_t i = 0; i < cats.size(); ++i) {
        if (cats[i].id == cat.id) { cats[i] = cat; found = true; break; }
    }
    if (!found) cats.push_back(cat);
    return saveAll(cats, items);
}

static void collectSubIds(const std::vector<Category>& all, int pid, std::vector<int>& ids) {
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].parentId == pid) { ids.push_back(all[i].id); collectSubIds(all, all[i].id, ids); }
    }
}

bool remove(int id) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::vector<int> removeIds;
    removeIds.push_back(id);
    collectSubIds(cats, id, removeIds);

    for (std::vector<Category>::iterator it = cats.begin(); it != cats.end(); ) {
        bool shouldRemove = false;
        for (size_t i = 0; i < removeIds.size(); ++i) { if (removeIds[i] == it->id) { shouldRemove = true; break; } }
        if (shouldRemove) it = cats.erase(it); else ++it;
    }

    for (std::vector<CategoryItemRecord>::iterator it = items.begin(); it != items.end(); ) {
        bool shouldRemove = false;
        for (size_t i = 0; i < removeIds.size(); ++i) { if (removeIds[i] == it->categoryId) { shouldRemove = true; break; } }
        if (shouldRemove) it = items.erase(it); else ++it;
    }

    return saveAll(cats, items);
}

bool reorder(int parentId, bool ascending) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::vector<Category*> targets;
    for (size_t i = 0; i < cats.size(); ++i) if (cats[i].parentId == parentId) targets.push_back(&cats[i]);
    
    struct Sorter {
        bool asc;
        bool operator()(Category* a, Category* b) const {
            int cmp = a->name.compare(b->name);
            return asc ? (cmp < 0) : (cmp > 0);
        }
    } sorter;
    sorter.asc = ascending;
    std::sort(targets.begin(), targets.end(), sorter);

    for (size_t i = 0; i < targets.size(); ++i) targets[i]->sortOrder = static_cast<int>(i);
    return saveAll(cats, items);
}

bool reorderAll(bool ascending) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    struct Sorter {
        bool asc;
        bool operator()(const Category& a, const Category& b) const {
            int cmp = a.name.compare(b.name);
            return asc ? (cmp < 0) : (cmp > 0);
        }
    } sorter;
    sorter.asc = ascending;
    std::sort(cats.begin(), cats.end(), sorter);

    for (size_t i = 0; i < cats.size(); ++i) cats[i].sortOrder = static_cast<int>(i);
    return saveAll(cats, items);
}

bool addItemToCategory(int categoryId, const std::string& fileId128) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    for (size_t i = 0; i < items.size(); ++i) if (items[i].categoryId == categoryId && items[i].fileId128 == fileId128) return true;
    items.push_back(CategoryItemRecord(categoryId, fileId128, static_cast<double>(QDateTime::currentMSecsSinceEpoch())));
    return saveAll(cats, items);
}

bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    for (std::vector<CategoryItemRecord>::iterator it = items.begin(); it != items.end(); ) {
        if (it->categoryId == categoryId && it->fileId128 == fileId128) it = items.erase(it); else ++it;
    }
    return saveAll(cats, items);
}

std::vector<std::string> getFileIdsInCategory(int categoryId) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::vector<std::string> results;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].categoryId == categoryId && folderIds.find(items[i].fileId128) == folderIds.end()) {
            results.push_back(items[i].fileId128);
        }
    }
    return results;
}

std::vector<std::pair<int, int> > getCounts() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    loadAll(cats, records);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    QMap<int, std::unordered_set<std::string> > catFileSets;
    for (size_t i = 0; i < records.size(); ++i) {
        const CategoryItemRecord& r = records[i];
        if (!r.fileId128.empty() && folderIds.find(r.fileId128) == folderIds.end()) {
            catFileSets[r.categoryId].insert(r.fileId128);
        }
    }

    std::vector<std::pair<int, int> > res;
    for (QMap<int, std::unordered_set<std::string> >::const_iterator it = catFileSets.constBegin(); it != catFileSets.constEnd(); ++it) {
        res.push_back(std::make_pair(it.key(), static_cast<int>(it.value().size())));
    }
    return res;
}

std::vector<std::string> getFileIdsRecursive(int categoryId) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::vector<int> ids;
    ids.push_back(categoryId);
    collectSubIds(cats, categoryId, ids);
    std::vector<std::string> results;
    for (size_t i = 0; i < items.size(); ++i) {
        const CategoryItemRecord& r = items[i];
        bool foundId = false;
        for (size_t j = 0; j < ids.size(); ++j) { if (ids[j] == r.categoryId) { foundId = true; break; } }
        if (foundId && folderIds.find(r.fileId128) == folderIds.end()) {
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
std::vector<std::pair<int, int> > CategoryRepo::getCounts() { return ScchCategoryEngine::getCounts(); }

int CategoryRepo::getUniqueItemCount() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    ScchCategoryEngine::loadAll(cats, records);

    std::unordered_set<std::string> folderIds;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (meta.isFolder) folderIds.insert(meta.fileId128);
    });

    std::unordered_set<std::string> uniqueIds;
    for (size_t i = 0; i < records.size(); ++i) {
        const CategoryItemRecord& r = records[i];
        if (!r.fileId128.empty() && folderIds.find(r.fileId128) == folderIds.end()) {
            uniqueIds.insert(r.fileId128);
        }
    }
    return static_cast<int>(uniqueIds.size());
}

int CategoryRepo::getUncategorizedItemCount() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> records;
    ScchCategoryEngine::loadAll(cats, records);
    std::unordered_set<std::string> categorizedIds;
    for (size_t i = 0; i < records.size(); ++i) {
        const CategoryItemRecord& r = records[i];
        if (!r.fileId128.empty()) categorizedIds.insert(r.fileId128);
    }

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

    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    double startOfToday = static_cast<double>(QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
    double startOfYesterday = startOfToday - 86400000.0;

    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (meta.isFolder) return;
        all++;
        if (meta.tags.isEmpty()) untagged++;
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
