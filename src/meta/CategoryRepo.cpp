#include "CategoryRepo.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QTimer>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <unordered_set>
#include "MetadataManager.h"

namespace ArcMeta {

/**
 * @brief CategoryItemRecord Structure
 * 2026-06-xx 物理加固：增加 pathHint 以便在 File ID 匹配失败时进行找回
 */
struct CategoryItemRecord {
    int categoryId;
    std::string fileId128;
    std::wstring pathHint; // 物理路径提示
    double addedAt;

    CategoryItemRecord() : categoryId(0), addedAt(0.0) {}
    CategoryItemRecord(int catId, const std::string& fid, const std::wstring& path, double time)
        : categoryId(catId), fileId128(fid), pathHint(path), addedAt(time) {}
};

/**
 * @brief Anonymous Namespace for Internal Operators
 */
namespace {
    QDataStream& operator<<(QDataStream& ds, const std::string& s) {
        ds << QString::fromStdString(s);
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, std::string& s) {
        QString qs; ds >> qs; s = qs.toStdString();
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const Category& c) {
        ds << c.id << c.parentId << QString::fromStdWString(c.name) << QString::fromStdWString(c.color);
        ds << static_cast<int>(c.presetTags.size());
        for (size_t i = 0; i < c.presetTags.size(); ++i) {
            ds << QString::fromStdWString(c.presetTags[i]);
        }
        ds << c.sortOrder << c.pinned << c.encrypted << QString::fromStdWString(c.encryptHint);
        return ds;
    }

    QDataStream& operator>>(QDataStream& ds, Category& c) {
        QString name, color, hint;
        ds >> c.id >> c.parentId >> name >> color;
        c.name = name.toStdWString(); c.color = color.toStdWString();
        int tagCount = 0; ds >> tagCount;
        c.presetTags.clear();
        for (int i = 0; i < tagCount; ++i) {
            QString t; ds >> t; c.presetTags.push_back(t.toStdWString());
        }
        ds >> c.sortOrder >> c.pinned >> c.encrypted >> hint;
        c.encryptHint = hint.toStdWString();
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const CategoryItemRecord& r) {
        // 版本 4 引入 pathHint
        ds << r.categoryId << QString::fromStdString(r.fileId128) << QString::fromStdWString(r.pathHint) << r.addedAt;
        return ds;
    }
}

/**
 * @brief 分类内存缓存管理器 (单例)
 * 2026-06-xx 架构升级：引入增量缓存与延迟写入，彻底解决 IO 性能瓶颈
 */
class CategoryCacheManager : public QObject {
    Q_OBJECT
public:
    static CategoryCacheManager& instance() {
        static CategoryCacheManager inst;
        return inst;
    }

    void ensureLoaded() {
        if (m_loaded) return;
        loadFromDisk();
        m_loaded = true;
    }

    std::vector<Category>& categories() { ensureLoaded(); return m_categories; }
    std::vector<CategoryItemRecord>& records() { ensureLoaded(); return m_records; }

    void markDirty() {
        m_dirty = true;
        if (!m_saveTimer->isActive()) {
            m_saveTimer->start(2000); // 2秒防抖写入
        }
    }

    void markSysCountsDirty() {
        m_sysCountsDirty = true;
    }

    QMap<QString, int> getSystemCounts() {
        ensureLoaded();
        if (!m_sysCountsDirty) return m_sysCountsCache;

        QMap<QString, int> counts;
        int all = 0, today = 0, yesterday = 0, recentlyVisited = 0, untagged = 0;

        double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        double startOfToday = static_cast<double>(QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
        double startOfYesterday = startOfToday - 86400000.0;

        std::unordered_set<std::string> categorizedIds;
        for (const auto& r : m_records) categorizedIds.insert(r.fileId128);

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

        // 计算未分类
        int uncategorizedCount = 0;
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
            if (!meta.isFolder && categorizedIds.find(meta.fileId128) == categorizedIds.end()) {
                uncategorizedCount++;
            }
        });
        counts["uncategorized"] = uncategorizedCount;
        counts["trash"] = 0;

        m_sysCountsCache = counts;
        m_sysCountsDirty = false;
        return counts;
    }

    void saveImmediately() {
        if (m_dirty) {
            saveToDisk();
            m_saveTimer->stop();
        }
    }

private:
    CategoryCacheManager() : m_loaded(false), m_dirty(false) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        connect(m_saveTimer, &QTimer::timeout, [this]() {
            saveToDisk();
        });

        // 监听元数据变更以失效计数缓存
        connect(&MetadataManager::instance(), &MetadataManager::metaChanged, this, [this]() {
            markSysCountsDirty();
        });
    }

    struct CategoryHeader {
        char magic[4];
        uint32_t version;
        uint32_t catCount;
        uint32_t itemCount;

        CategoryHeader() : version(4), catCount(0), itemCount(0) {
            magic[0] = 'C'; magic[1] = 'A'; magic[2] = 'T'; magic[3] = 'S';
        }
    };

    void loadFromDisk() {
        QFile file("arcmeta_categories.scch");
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;

        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        CategoryHeader header;
        if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return;
        if (memcmp(header.magic, "CATS", 4) != 0) return;

        m_categories.clear();
        for (uint32_t i = 0; i < header.catCount; ++i) {
            Category c; ds >> c; m_categories.push_back(c);
        }

        m_records.clear();
        for (uint32_t i = 0; i < header.itemCount; ++i) {
            CategoryItemRecord r;
            if (header.version >= 4) {
                QString fid, path;
                ds >> r.categoryId >> fid >> path >> r.addedAt;
                r.fileId128 = fid.toStdString();
                r.pathHint = path.toStdWString();
            } else {
                // 兼容旧版 V3
                QString fid;
                ds >> r.categoryId >> fid >> r.addedAt;
                r.fileId128 = fid.toStdString();
            }
            m_records.push_back(r);
        }
    }

    void saveToDisk() {
        if (!m_dirty) return;

        QFile file("arcmeta_categories.scch.tmp");
        if (!file.open(QIODevice::WriteOnly)) return;

        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        CategoryHeader header;
        header.catCount = static_cast<uint32_t>(m_categories.size());
        header.itemCount = static_cast<uint32_t>(m_records.size());
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        for (const auto& c : m_categories) ds << c;
        for (const auto& r : m_records) ds << r;

        file.close();
        QFile::remove("arcmeta_categories.scch");
        QFile::rename("arcmeta_categories.scch.tmp", "arcmeta_categories.scch");

        m_dirty = false;
    }

    std::vector<Category> m_categories;
    std::vector<CategoryItemRecord> m_records;
    bool m_loaded;
    bool m_dirty;
    QTimer* m_saveTimer;

    // 系统分类增量计数缓存
    mutable QMap<QString, int> m_sysCountsCache;
    mutable bool m_sysCountsDirty = true;
};

namespace ScchCategoryEngine {

std::vector<Category> getAll() {
    auto& cats = CategoryCacheManager::instance().categories();
    std::vector<Category> results = cats;
    std::sort(results.begin(), results.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });
    return results;
}

bool add(Category& cat) {
    auto& cats = CategoryCacheManager::instance().categories();
    int maxId = 0;
    for (const auto& c : cats) if (c.id > maxId) maxId = c.id;
    cat.id = maxId + 1;
    cats.push_back(cat);
    CategoryCacheManager::instance().markDirty();
    return true;
}

bool update(const Category& cat) {
    auto& cats = CategoryCacheManager::instance().categories();
    bool found = false;
    for (auto& c : cats) {
        if (c.id == cat.id) { c = cat; found = true; break; }
    }
    if (!found) cats.push_back(cat);
    CategoryCacheManager::instance().markDirty();
    return true;
}

static void collectSubIds(const std::vector<Category>& all, int pid, std::vector<int>& ids) {
    for (const auto& c : all) {
        if (c.parentId == pid) { ids.push_back(c.id); collectSubIds(all, c.id, ids); }
    }
}

bool remove(int id) {
    auto& cats = CategoryCacheManager::instance().categories();
    auto& records = CategoryCacheManager::instance().records();

    std::vector<int> removeIds;
    removeIds.push_back(id);
    collectSubIds(cats, id, removeIds);

    auto itCat = std::remove_if(cats.begin(), cats.end(), [&](const Category& c) {
        return std::find(removeIds.begin(), removeIds.end(), c.id) != removeIds.end();
    });
    if (itCat != cats.end()) {
        cats.erase(itCat, cats.end());
        CategoryCacheManager::instance().markDirty();
    }

    auto itRec = std::remove_if(records.begin(), records.end(), [&](const CategoryItemRecord& r) {
        return std::find(removeIds.begin(), removeIds.end(), r.categoryId) != removeIds.end();
    });
    if (itRec != records.end()) {
        records.erase(itRec, records.end());
        CategoryCacheManager::instance().markDirty();
    }

    return true;
}

bool reorder(int parentId, bool ascending) {
    auto& cats = CategoryCacheManager::instance().categories();
    std::vector<Category*> targets;
    for (auto& c : cats) if (c.parentId == parentId) targets.push_back(&c);
    
    std::sort(targets.begin(), targets.end(), [ascending](Category* a, Category* b) {
        int cmp = a->name.compare(b->name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    for (size_t i = 0; i < targets.size(); ++i) targets[i]->sortOrder = static_cast<int>(i);
    CategoryCacheManager::instance().markDirty();
    return true;
}

bool reorderAll(bool ascending) {
    auto& cats = CategoryCacheManager::instance().categories();
    std::sort(cats.begin(), cats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    for (size_t i = 0; i < cats.size(); ++i) cats[i].sortOrder = static_cast<int>(i);
    CategoryCacheManager::instance().markDirty();
    return true;
}

bool addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    auto& records = CategoryCacheManager::instance().records();
    for (const auto& r : records) {
        if (r.categoryId == categoryId && r.fileId128 == fileId128) return true;
    }

    std::wstring finalPathHint = pathHint;
    if (finalPathHint.empty()) {
        finalPathHint = MetadataManager::instance().getPathByFid(fileId128);
    }

    records.push_back(CategoryItemRecord(categoryId, fileId128, finalPathHint,
        static_cast<double>(QDateTime::currentMSecsSinceEpoch())));

    CategoryCacheManager::instance().markDirty();
    CategoryCacheManager::instance().markSysCountsDirty();
    return true;
}

bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    auto& records = CategoryCacheManager::instance().records();
    auto it = std::remove_if(records.begin(), records.end(), [&](const CategoryItemRecord& r) {
        return r.categoryId == categoryId && r.fileId128 == fileId128;
    });
    if (it != records.end()) {
        records.erase(it, records.end());
        CategoryCacheManager::instance().markDirty();
        CategoryCacheManager::instance().markSysCountsDirty();
        return true;
    }
    return false;
}

std::vector<CategoryItem> getItemsInCategory(int categoryId) {
    auto& records = CategoryCacheManager::instance().records();
    std::vector<CategoryItem> results;
    for (const auto& r : records) {
        if (r.categoryId == categoryId) {
            results.push_back({r.fileId128, r.pathHint});
        }
    }
    return results;
}

std::vector<CategoryItem> getItemsRecursive(int categoryId) {
    auto& cats = CategoryCacheManager::instance().categories();
    auto& records = CategoryCacheManager::instance().records();

    std::vector<int> ids;
    ids.push_back(categoryId);
    collectSubIds(cats, categoryId, ids);

    std::unordered_set<int> idSet(ids.begin(), ids.end());
    std::map<std::string, std::wstring> resultsMap; // 用 map 去重且保留 pathHint

    for (const auto& r : records) {
        if (idSet.count(r.categoryId)) {
            resultsMap[r.fileId128] = r.pathHint;
        }
    }

    std::vector<CategoryItem> results;
    for (auto const& [fid, path] : resultsMap) {
        results.push_back({fid, path});
    }
    return results;
}

std::vector<std::pair<int, int>> getCounts() {
    auto& records = CategoryCacheManager::instance().records();
    QMap<int, std::unordered_set<std::string>> catFileSets;

    for (const auto& r : records) {
        if (r.fileId128.empty()) continue;

        // 物理存在性校验
        std::wstring path = MetadataManager::instance().getPathByFid(r.fileId128);
        if (path.empty() && !r.pathHint.empty()) {
            // 尝试通过 pathHint 找回（如果文件还在原位但 FRN 变了，MetadataManager 会在此之后同步）
            path = r.pathHint;
        }

        if (!path.empty()) {
            RuntimeMeta meta = MetadataManager::instance().getMeta(path);
            if (!meta.isFolder) {
                catFileSets[r.categoryId].insert(r.fileId128);
            }
        }
    }

    std::vector<std::pair<int, int>> res;
    for (auto it = catFileSets.constBegin(); it != catFileSets.constEnd(); ++it) {
        res.push_back(std::make_pair(it.key(), static_cast<int>(it.value().size())));
    }
    return res;
}

std::vector<std::string> getFileIdsRecursive(int categoryId) {
    auto& cats = CategoryCacheManager::instance().categories();
    auto& records = CategoryCacheManager::instance().records();

    std::vector<int> ids;
    ids.push_back(categoryId);
    collectSubIds(cats, categoryId, ids);

    std::unordered_set<int> idSet(ids.begin(), ids.end());
    std::unordered_set<std::string> resultsSet;

    for (const auto& r : records) {
        if (idSet.count(r.categoryId)) {
            resultsSet.insert(r.fileId128);
        }
    }

    return std::vector<std::string>(resultsSet.begin(), resultsSet.end());
}

} // namespace ScchCategoryEngine

// CategoryRepo Implementation
std::vector<Category> CategoryRepo::getAll() { return ScchCategoryEngine::getAll(); }
std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    auto& records = CategoryCacheManager::instance().records();
    auto& allCats = CategoryCacheManager::instance().categories();

    // 1. 统计每个分类的最近使用时间 (基于关联记录的 addedAt)
    std::map<int, double> lastUsedMap;
    for (const auto& r : records) {
        if (r.addedAt > lastUsedMap[r.categoryId]) {
            lastUsedMap[r.categoryId] = r.addedAt;
        }
    }

    // 2. 将全量分类进行排序
    std::vector<Category> sortedCats = allCats;
    std::sort(sortedCats.begin(), sortedCats.end(), [&](const Category& a, const Category& b) {
        return lastUsedMap[a.id] > lastUsedMap[b.id];
    });

    if (sortedCats.size() > (size_t)limit) sortedCats.resize(limit);
    return sortedCats;
}
bool CategoryRepo::add(Category& cat) { return ScchCategoryEngine::add(cat); }
bool CategoryRepo::update(const Category& cat) { return ScchCategoryEngine::update(cat); }
bool CategoryRepo::remove(int id) { return ScchCategoryEngine::remove(id); }
bool CategoryRepo::reorder(int parentId, bool ascending) { return ScchCategoryEngine::reorder(parentId, ascending); }
bool CategoryRepo::reorderAll(bool ascending) { return ScchCategoryEngine::reorderAll(ascending); }

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    return ScchCategoryEngine::addItemToCategory(categoryId, fileId128, pathHint);
}
bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) { return ScchCategoryEngine::removeItemFromCategory(categoryId, fileId128); }
std::vector<CategoryItem> CategoryRepo::getItemsInCategory(int categoryId) { return ScchCategoryEngine::getItemsInCategory(categoryId); }
std::vector<CategoryItem> CategoryRepo::getItemsRecursive(int categoryId) { return ScchCategoryEngine::getItemsRecursive(categoryId); }

std::vector<std::string> CategoryRepo::getFileIdsInCategory(int categoryId) {
    auto items = ScchCategoryEngine::getItemsInCategory(categoryId);
    std::vector<std::string> res;
    for(auto& i : items) res.push_back(i.fileId128);
    return res;
}
std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) {
    auto items = ScchCategoryEngine::getItemsRecursive(categoryId);
    std::vector<std::string> res;
    for(auto& i : items) res.push_back(i.fileId128);
    return res;
}
std::vector<std::pair<int, int>> CategoryRepo::getCounts() { return ScchCategoryEngine::getCounts(); }

int CategoryRepo::getUniqueItemCount() {
    auto& records = CategoryCacheManager::instance().records();
    std::unordered_set<std::string> uniqueIds;
    for (const auto& r : records) {
        if (!r.fileId128.empty()) {
            std::wstring path = MetadataManager::instance().getPathByFid(r.fileId128);
            if (!path.empty()) {
                RuntimeMeta meta = MetadataManager::instance().getMeta(path);
                if (!meta.isFolder) uniqueIds.insert(r.fileId128);
            }
        }
    }
    return static_cast<int>(uniqueIds.size());
}

int CategoryRepo::getUncategorizedItemCount() {
    auto& records = CategoryCacheManager::instance().records();
    std::unordered_set<std::string> categorizedIds;
    for (const auto& r : records) categorizedIds.insert(r.fileId128);

    int count = 0;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) {
        if (!meta.isFolder && categorizedIds.find(meta.fileId128) == categorizedIds.end()) {
            count++;
        }
    });
    return count;
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    return CategoryCacheManager::instance().getSystemCounts();
}

QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    std::unordered_set<std::string> categorizedIds;
    if (type == "uncategorized") {
        for (const auto& r : CategoryCacheManager::instance().records()) categorizedIds.insert(r.fileId128);
    }

    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    double startOfToday = static_cast<double>(QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
    double startOfYesterday = startOfToday - 86400000.0;

    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.isFolder) return;
        bool match = false;
        if (type == "all") match = true;
        else if (type == "untagged" && meta.tags.isEmpty()) match = true;
        else if (type == "today" && (meta.ctime >= startOfToday || meta.mtime >= startOfToday)) match = true;
        else if (type == "yesterday" && (meta.ctime >= startOfYesterday || meta.mtime >= startOfYesterday) && (meta.ctime < startOfToday && meta.mtime < startOfToday)) match = true;
        else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
        else if (type == "uncategorized" && categorizedIds.find(meta.fileId128) == categorizedIds.end()) match = true;

        if (match) paths << QString::fromStdWString(path);
    });
    return paths;
}

} // namespace ArcMeta

#include "CategoryRepo.moc"
