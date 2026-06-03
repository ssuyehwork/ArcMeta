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
    std::vector<std::string> results;
    for (const auto& r : items) if (r.categoryId == categoryId) results.push_back(r.fileId128);
    return results;
}

std::vector<std::pair<int, int>> getCounts() {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::map<int, int> m;
    for (const auto& r : items) m[r.categoryId]++;
    std::vector<std::pair<int, int>> res;
    for (auto const& [id, count] : m) res.push_back({id, count});
    return res;
}

std::vector<std::string> getFileIdsRecursive(int categoryId) {
    std::vector<Category> cats;
    std::vector<CategoryItemRecord> items;
    loadAll(cats, items);
    std::vector<int> ids = {categoryId};
    collectSubIds(cats, categoryId, ids);
    std::vector<std::string> results;
    for (const auto& r : items) {
        if (std::find(ids.begin(), ids.end(), r.categoryId) != ids.end()) results.push_back(r.fileId128);
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
int CategoryRepo::getUniqueItemCount() { return 0; }
int CategoryRepo::getUncategorizedItemCount() { return 0; }
QMap<QString, int> CategoryRepo::getSystemCounts() { return QMap<QString, int>(); }
bool CategoryRepo::remove(int id) { return ScchCategoryEngine::remove(id); }
bool CategoryRepo::reorder(int parentId, bool ascending) { return ScchCategoryEngine::reorder(parentId, ascending); }
std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) { return ScchCategoryEngine::getFileIdsRecursive(categoryId); }

} // namespace ArcMeta
