#include "CategoryRepo.h"
#include <QDataStream>
#include <QDateTime>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <unordered_set>
#include "MetadataManager.h"

namespace ArcMeta {

static std::wstring normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path)));
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    if (qp.length() >= 2 && qp[1] == ':') qp[0] = qp[0].toUpper();
    return qp.toStdWString();
}

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
#include <QRecursiveMutex>
#include <QMutexLocker>

class CategoryCacheManager : public QObject {
    Q_OBJECT
public:
    mutable QRecursiveMutex m_mutex;

    // 统计加速层 (公开以允许同文件引擎函数直接访问)
    mutable QMap<QString, int> m_sysCountsCache;
    mutable bool m_sysCountsDirty = true;
    std::unordered_map<std::wstring, uint8_t> m_membershipMap;
    std::unordered_map<std::string, int> m_fidCategorizedCount;
    QDate m_lastCountDate;

    mutable QMap<int, int> m_catCountsCache;
    mutable bool m_catCountsDirty = true;

    static CategoryCacheManager& instance() {
        static CategoryCacheManager inst;
        return inst;
    }

    ~CategoryCacheManager() {
        saveImmediately();
    }

    void ensureLoaded() {
        QMutexLocker locker(&m_mutex);
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

    void markSysCountsDirty() { m_sysCountsDirty = true; }
    void markCatCountsDirty() { m_catCountsDirty = true; }

    QMap<QString, int> getSystemCounts() {
        ensureLoaded();
        if (m_sysCountsDirty || m_lastCountDate != QDate::currentDate()) {
            fullRecount();
        }
        return m_sysCountsCache;
    }

    void updateFidCategorized(const std::string& fid, int delta, int categoryId) {
        if (fid.empty()) return;
        QMutexLocker locker(&m_mutex);
        int oldCount = m_fidCategorizedCount[fid];
        int newCount = std::max(0, oldCount + delta);
        m_fidCategorizedCount[fid] = newCount;

        if ((oldCount == 0 && newCount > 0) || (oldCount > 0 && newCount == 0)) {
            std::wstring path = MetadataManager::instance().getPathByFid(fid);
            if (!path.empty()) {
                updateIncremental(path);
            }
        }

        // 增量更新特定分类计数
        if (!m_catCountsDirty) {
            m_catCountsCache[categoryId] = std::max(0, m_catCountsCache[categoryId] + delta);
        } else {
            markCatCountsDirty();
        }
    }

    void fullRecount() {
        m_sysCountsCache.clear();
        m_membershipMap.clear();

        int all = 0, today = 0, yesterday = 0, recently = 0, untagged = 0, uncategorized = 0;

        double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        double startOfToday = static_cast<double>(QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
        double startOfYesterday = startOfToday - 86400000.0;

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            if (meta.isFolder) return;

            uint8_t bits = 0;
            bits |= 1; // All
            all++;

            if (meta.tags.isEmpty()) { bits |= 16; untagged++; }
            if (meta.ctime >= startOfToday || meta.mtime >= startOfToday) { bits |= 2; today++; }
            if ((meta.ctime >= startOfYesterday || meta.mtime >= startOfYesterday) &&
                (meta.ctime < startOfToday && meta.mtime < startOfToday)) { bits |= 4; yesterday++; }
            if (meta.atime >= now - 86400000.0) { bits |= 8; recently++; }

            if (m_fidCategorizedCount[meta.fileId128] == 0) { bits |= 32; uncategorized++; }

            m_membershipMap[path] = bits;
        });

        m_sysCountsCache["all"] = all;
        m_sysCountsCache["today"] = today;
        m_sysCountsCache["yesterday"] = yesterday;
        m_sysCountsCache["recently_visited"] = recently;
        m_sysCountsCache["untagged"] = untagged;
        m_sysCountsCache["uncategorized"] = uncategorized;
        m_sysCountsCache["trash"] = 0;

        m_sysCountsDirty = false;
        m_lastCountDate = QDate::currentDate();
    }

    void updateIncremental(const std::wstring& path) {
        if (path == L"__RELOAD_ALL__") {
            fullRecount();
            return;
        }

        if (m_sysCountsDirty) return;

        if (m_lastCountDate != QDate::currentDate()) {
            fullRecount();
            return;
        }

        RuntimeMeta meta = MetadataManager::instance().getMeta(path);

        double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        double startOfToday = static_cast<double>(QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch());
        double startOfYesterday = startOfToday - 86400000.0;

        uint8_t newBits = 0;
        if (!meta.isFolder) {
            newBits |= 1;
            if (meta.tags.isEmpty()) newBits |= 16;
            if (meta.ctime >= startOfToday || meta.mtime >= startOfToday) newBits |= 2;
            if ((meta.ctime >= startOfYesterday || meta.mtime >= startOfYesterday) &&
                (meta.ctime < startOfToday && meta.mtime < startOfToday)) newBits |= 4;
            if (meta.atime >= now - 86400000.0) newBits |= 8;
            if (m_fidCategorizedCount[meta.fileId128] == 0) newBits |= 32;
        }

        uint8_t oldBits = 0;
        auto it = m_membershipMap.find(path);
        if (it != m_membershipMap.end()) oldBits = it->second;

        if (newBits == oldBits) return;

        auto updateCount = [&](uint8_t bit, const QString& key) {
            if ((newBits & bit) && !(oldBits & bit)) m_sysCountsCache[key]++;
            else if (!(newBits & bit) && (oldBits & bit)) m_sysCountsCache[key]--;
        };

        updateCount(1, "all");
        updateCount(2, "today");
        updateCount(4, "yesterday");
        updateCount(8, "recently_visited");
        updateCount(16, "untagged");
        updateCount(32, "uncategorized");

        if (newBits == 0) m_membershipMap.erase(path);
        else m_membershipMap[path] = newBits;
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

        connect(&MetadataManager::instance(), &MetadataManager::metaChanged, this, [this](const QString& path) {
            updateIncremental(path.toStdWString());
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
        m_fidCategorizedCount.clear();
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
            if (!r.fileId128.empty()) m_fidCategorizedCount[r.fileId128]++;
        }
        m_catCountsDirty = true;
        m_sysCountsDirty = true;
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

    std::vector<CategoryItemRecord> keptRecords;
    bool changed = false;
    std::unordered_set<int> removeIdSet(removeIds.begin(), removeIds.end());

    for (auto& r : records) {
        if (removeIdSet.count(r.categoryId)) {
             CategoryCacheManager::instance().updateFidCategorized(r.fileId128, -1);
             changed = true;
        } else {
             keptRecords.push_back(r);
        }
    }
    if (changed) {
        records = keptRecords;
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

    std::wstring finalPathHint = normalizePath(pathHint);
    if (finalPathHint.empty()) {
        finalPathHint = MetadataManager::instance().getPathByFid(fileId128);
    }

    records.push_back(CategoryItemRecord(categoryId, fileId128, finalPathHint,
        static_cast<double>(QDateTime::currentMSecsSinceEpoch())));

    CategoryCacheManager::instance().updateFidCategorized(fileId128, 1, categoryId);
    CategoryCacheManager::instance().markDirty();
    return true;
}

bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    auto& records = CategoryCacheManager::instance().records();
    auto it = std::find_if(records.begin(), records.end(), [&](const CategoryItemRecord& r) {
        return r.categoryId == categoryId && r.fileId128 == fileId128;
    });
    if (it != records.end()) {
        records.erase(it);
        CategoryCacheManager::instance().updateFidCategorized(fileId128, -1, categoryId);
        CategoryCacheManager::instance().markDirty();
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
    auto& inst = CategoryCacheManager::instance();
    inst.ensureLoaded();

    if (inst.m_catCountsDirty) {
        inst.m_catCountsCache.clear();
        QMap<int, std::unordered_set<std::string>> catFileSets;
        for (const auto& r : inst.records()) {
            if (r.fileId128.empty()) continue;
            // 2026-06-xx 物理脱钩：不再在计数时执行耗时的物理校验，支持幽灵项显示
            catFileSets[r.categoryId].insert(r.fileId128);
        }
        for (auto it = catFileSets.constBegin(); it != catFileSets.constEnd(); ++it) {
            inst.m_catCountsCache[it.key()] = static_cast<int>(it.value().size());
        }
        inst.m_catCountsDirty = false;
    }

    std::vector<std::pair<int, int>> res;
    for (auto it = inst.m_catCountsCache.constBegin(); it != inst.m_catCountsCache.constEnd(); ++it) {
        res.push_back({it.key(), it.value()});
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
    return CategoryCacheManager::instance().getSystemCounts().value("all", 0);
}

int CategoryRepo::getUncategorizedItemCount() {
    return CategoryCacheManager::instance().getSystemCounts().value("uncategorized", 0);
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
