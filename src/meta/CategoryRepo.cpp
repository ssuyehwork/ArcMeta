#include "CategoryRepo.h"
#include <QDataStream>
#include <QDateTime>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QRecursiveMutex>
#include <QMetaObject>
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
class CategoryCacheManager : public QObject {
    Q_OBJECT
    mutable QRecursiveMutex m_mutex;
public:
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

    void ensureLoaded() {
        QMutexLocker locker(&m_mutex);
        if (m_loaded) return;
        loadFromDisk();
        m_loaded = true;
    }

    void forEachCategory(const std::function<void(const Category&)>& func) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        for (const auto& c : m_categories) func(c);
    }

    void forEachRecord(const std::function<void(const CategoryItemRecord&)>& func) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        for (const auto& r : m_records) func(r);
    }

    // 允许在锁保护下直接按 ID 获取分类
    bool findCategory(int id, Category& out) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        for (const auto& c : m_categories) {
            if (c.id == id) { out = c; return true; }
        }
        return false;
    }

    // 在导入时非常有用：根据父ID和名称查找 (O(1) 索引优化)
    int findCategoryId(int parentId, const std::wstring& name) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        auto it = m_categoryLookup.find({parentId, name});
        return (it != m_categoryLookup.end()) ? it->second : 0;
    }

    void addCategory(Category& cat) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        cat.id = ++m_maxCategoryId;
        m_categories.push_back(cat);
        m_categoryLookup[{cat.parentId, cat.name}] = cat.id;
        markDirty();
    }

    void updateCategory(const Category& cat) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        for (auto& c : m_categories) {
            if (c.id == cat.id) {
                // 如果父级或名称发生变化，更新索引
                if (c.parentId != cat.parentId || c.name != cat.name) {
                    m_categoryLookup.erase({c.parentId, c.name});
                    m_categoryLookup[{cat.parentId, cat.name}] = cat.id;
                }
                c = cat;
                markDirty();
                return;
            }
        }
        // 若不存在则视为新增 (兜底逻辑)
        Category newCat = cat;
        addCategory(newCat);
    }

    void removeCategory(int id) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        
        std::vector<int> removeIds;
        removeIds.push_back(id);
        
        // 递归收集子分类
        std::function<void(int)> collect;
        collect = [&](int pid) {
            for (const auto& c : m_categories) {
                if (c.parentId == pid) {
                    removeIds.push_back(c.id);
                    collect(c.id);
                }
            }
        };
        collect(id);

        std::unordered_set<int> removeIdSet(removeIds.begin(), removeIds.end());

        // 清理分类列表与索引
        auto itCat = std::remove_if(m_categories.begin(), m_categories.end(), [&](const Category& c) {
            if (removeIdSet.count(c.id)) {
                m_categoryLookup.erase({c.parentId, c.name});
                return true;
            }
            return false;
        });
        if (itCat != m_categories.end()) {
            m_categories.erase(itCat, m_categories.end());
            markDirty();
        }

        // 清理关联记录与索引
        auto itRec = std::remove_if(m_records.begin(), m_records.end(), [&](const CategoryItemRecord& r) {
            if (removeIdSet.count(r.categoryId)) {
                m_itemLookup.erase(std::to_string(r.categoryId) + "_" + r.fileId128);
                updateFidCategorized(r.fileId128, -1);
                return true;
            }
            return false;
        });
        if (itRec != m_records.end()) {
            m_records.erase(itRec, m_records.end());
            markDirty();
        }
    }

    void addItem(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        
        std::string key = std::to_string(categoryId) + "_" + fileId128;
        if (m_itemLookup.count(key)) return;
        
        m_records.push_back(CategoryItemRecord(categoryId, fileId128, pathHint, 
            static_cast<double>(QDateTime::currentMSecsSinceEpoch())));
        
        m_itemLookup.insert(key);
        updateFidCategorized(fileId128, 1);
        markDirty();
    }

    void removeItem(int categoryId, const std::string& fileId128) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        auto it = std::find_if(m_records.begin(), m_records.end(), [&](const CategoryItemRecord& r) {
            return r.categoryId == categoryId && r.fileId128 == fileId128;
        });
        if (it != m_records.end()) {
            m_itemLookup.erase(std::to_string(categoryId) + "_" + fileId128);
            m_records.erase(it);
            updateFidCategorized(fileId128, -1);
            markDirty();
        }
    }

    void reorderCategories(int parentId, bool ascending) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        std::vector<Category*> targets;
        for (auto& c : m_categories) if (c.parentId == parentId) targets.push_back(&c);
        
        std::sort(targets.begin(), targets.end(), [ascending](Category* a, Category* b) {
            int cmp = a->name.compare(b->name);
            return ascending ? (cmp < 0) : (cmp > 0);
        });

        for (size_t i = 0; i < targets.size(); ++i) targets[i]->sortOrder = static_cast<int>(i);
        markDirty();
    }

    void reorderAllCategories(bool ascending) {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        std::sort(m_categories.begin(), m_categories.end(), [ascending](const Category& a, const Category& b) {
            int cmp = a.name.compare(b.name);
            return ascending ? (cmp < 0) : (cmp > 0);
        });

        for (size_t i = 0; i < m_categories.size(); ++i) {
            m_categories[i].sortOrder = static_cast<int>(i);
        }
        markDirty();
    }

    void markDirty() {
        m_dirty = true;
        // 2026-06-xx 修复：确保定时器在创建它的主线程中启动，解决后台线程直接调用导致的失效报警
        QMetaObject::invokeMethod(m_saveTimer, "start", Qt::QueuedConnection, Q_ARG(int, 2000));
    }

    std::vector<std::pair<int, int>> getCounts() {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        
        if (m_catCountsDirty) {
            m_catCountsCache.clear();
            QMap<int, std::unordered_set<std::string>> catFileSets;
            for (const auto& r : m_records) {
                if (r.fileId128.empty()) continue;
                // 2026-06-xx 物理脱钩：不再在计数时执行耗时的物理校验，支持幽灵项显示
                catFileSets[r.categoryId].insert(r.fileId128);
            }
            for (auto it = catFileSets.constBegin(); it != catFileSets.constEnd(); ++it) {
                m_catCountsCache[it.key()] = static_cast<int>(it.value().size());
            }
            m_catCountsDirty = false;
        }

        std::vector<std::pair<int, int>> res;
        for (auto it = m_catCountsCache.constBegin(); it != m_catCountsCache.constEnd(); ++it) {
            res.push_back({it.key(), it.value()});
        }
        return res;
    }

    void markSysCountsDirty() { QMutexLocker locker(&m_mutex); m_sysCountsDirty = true; }
    void markCatCountsDirty() { QMutexLocker locker(&m_mutex); m_catCountsDirty = true; }

    QMap<QString, int> getSystemCounts() {
        QMutexLocker locker(&m_mutex);
        ensureLoaded();
        if (m_sysCountsDirty || m_lastCountDate != QDate::currentDate()) {
            fullRecount();
        }
        return m_sysCountsCache;
    }

    void updateFidCategorized(const std::string& fid, int delta) {
        QMutexLocker locker(&m_mutex);
        if (fid.empty()) return;
        int oldCount = m_fidCategorizedCount[fid];
        int newCount = std::max(0, oldCount + delta);
        m_fidCategorizedCount[fid] = newCount;

        if ((oldCount == 0 && newCount > 0) || (oldCount > 0 && newCount == 0)) {
            std::wstring path = MetadataManager::instance().getPathByFid(fid);
            if (!path.empty()) {
                updateIncremental(path);
            }
        }
        markCatCountsDirty();
    }

    void fullRecount() {
        // 假定调用者已持有 m_mutex
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
        QMutexLocker locker(&m_mutex);
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
        QMutexLocker locker(&m_mutex);
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
        // 假定调用者已持有 m_mutex
        QFile file("arcmeta_categories.scch");
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;

        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        CategoryHeader header;
        if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return;
        if (memcmp(header.magic, "CATS", 4) != 0) return;

        m_categories.clear();
        m_categoryLookup.clear();
        m_maxCategoryId = 0;
        for (uint32_t i = 0; i < header.catCount; ++i) {
            Category c; ds >> c; 
            m_categories.push_back(c);
            m_categoryLookup[{c.parentId, c.name}] = c.id;
            if (c.id > m_maxCategoryId) m_maxCategoryId = c.id;
        }

        m_records.clear();
        m_itemLookup.clear();
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
            m_itemLookup.insert(std::to_string(r.categoryId) + "_" + r.fileId128);
            if (!r.fileId128.empty()) m_fidCategorizedCount[r.fileId128]++;
        }
        m_catCountsDirty = true;
        m_sysCountsDirty = true;
    }

    void saveToDisk() {
        // 假定调用者已持有 m_mutex
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
    std::map<std::pair<int, std::wstring>, int> m_categoryLookup;
    int m_maxCategoryId = 0;

    std::vector<CategoryItemRecord> m_records;
    std::unordered_set<std::string> m_itemLookup;

    bool m_loaded;
    bool m_dirty;
    QTimer* m_saveTimer;

};

namespace ScchCategoryEngine {

std::vector<Category> getAll() {
    std::vector<Category> results;
    CategoryCacheManager::instance().forEachCategory([&](const Category& c) {
        results.push_back(c);
    });
    std::sort(results.begin(), results.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });
    return results;
}

bool add(Category& cat) {
    CategoryCacheManager::instance().addCategory(cat);
    return true;
}

bool update(const Category& cat) {
    CategoryCacheManager::instance().updateCategory(cat);
    return true;
}

bool remove(int id) {
    CategoryCacheManager::instance().removeCategory(id);
    return true;
}

bool reorder(int parentId, bool ascending) {
    CategoryCacheManager::instance().reorderCategories(parentId, ascending);
    return true;
}

bool reorderAll(bool ascending) {
    CategoryCacheManager::instance().reorderAllCategories(ascending);
    return true;
}

bool addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    std::wstring finalPathHint = normalizePath(pathHint);
    if (finalPathHint.empty()) {
        finalPathHint = MetadataManager::instance().getPathByFid(fileId128);
    }
    CategoryCacheManager::instance().addItem(categoryId, fileId128, finalPathHint);
    return true;
}

bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    CategoryCacheManager::instance().removeItem(categoryId, fileId128);
    return true;
}

std::vector<CategoryItem> getItemsInCategory(int categoryId) {
    std::vector<CategoryItem> results;
    CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
        if (r.categoryId == categoryId) {
            results.push_back({r.fileId128, r.pathHint});
        }
    });
    return results;
}

std::vector<CategoryItem> getItemsRecursive(int categoryId) {
    std::vector<int> ids;
    ids.push_back(categoryId);
    
    // 我们需要在锁内完成 ID 收集以保证一致性
    // 既然 getAll 已经拷贝了一次，我们在此处直接操作内存或增加专用递归收集接口
    
    auto allCats = getAll(); // getAll 已经根据 sortOrder 排序并拷贝了，可用于递归
    
    std::function<void(int)> collect = [&](int pid) {
        for (const auto& c : allCats) {
            if (c.parentId == pid) { ids.push_back(c.id); collect(c.id); }
        }
    };
    collect(categoryId);
    
    std::unordered_set<int> idSet(ids.begin(), ids.end());
    std::map<std::string, std::wstring> resultsMap;
    
    CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
        if (idSet.count(r.categoryId)) {
            resultsMap[r.fileId128] = r.pathHint;
        }
    });
    
    std::vector<CategoryItem> results;
    for (auto const& [fid, path] : resultsMap) {
        results.push_back({fid, path});
    }
    return results;
}

std::vector<std::pair<int, int>> getCounts() {
    auto& inst = CategoryCacheManager::instance();
    return inst.getCounts();
}

std::vector<std::string> getFileIdsRecursive(int categoryId) {
    auto items = getItemsRecursive(categoryId);
    std::vector<std::string> res;
    for (const auto& i : items) res.push_back(i.fileId128);
    return res;
}

} // namespace ScchCategoryEngine

void CategoryRepo::saveImmediately() {
    CategoryCacheManager::instance().saveImmediately();
}

void CategoryRepo::initialize() {
    (void)CategoryCacheManager::instance();
}

// CategoryRepo Implementation
std::vector<Category> CategoryRepo::getAll() { return ScchCategoryEngine::getAll(); }
std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) { 
    // 1. 统计每个分类的最近使用时间 (基于关联记录的 addedAt)
    std::map<int, double> lastUsedMap;
    CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
        if (r.addedAt > lastUsedMap[r.categoryId]) {
            lastUsedMap[r.categoryId] = r.addedAt;
        }
    });

    // 2. 将全量分类进行排序
    std::vector<Category> sortedCats = getAll();
    std::sort(sortedCats.begin(), sortedCats.end(), [&](const Category& a, const Category& b) {
        double timeA = lastUsedMap.count(a.id) ? lastUsedMap[a.id] : 0.0;
        double timeB = lastUsedMap.count(b.id) ? lastUsedMap[b.id] : 0.0;
        return timeA > timeB;
    });

    if (sortedCats.size() > (size_t)limit) sortedCats.resize(limit);
    return sortedCats;
}
bool CategoryRepo::add(Category& cat) { return ScchCategoryEngine::add(cat); }
bool CategoryRepo::update(const Category& cat) { return ScchCategoryEngine::update(cat); }
int CategoryRepo::findCategoryId(int parentId, const std::wstring& name) {
    return CategoryCacheManager::instance().findCategoryId(parentId, name);
}
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
    std::unordered_set<std::string> uniqueIds;
    CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
        if (!r.fileId128.empty()) {
            std::wstring path = MetadataManager::instance().getPathByFid(r.fileId128);
            if (!path.empty()) {
                RuntimeMeta meta = MetadataManager::instance().getMeta(path);
                if (!meta.isFolder) uniqueIds.insert(r.fileId128);
            }
        }
    });
    return static_cast<int>(uniqueIds.size());
}

int CategoryRepo::getUncategorizedItemCount() {
    std::unordered_set<std::string> categorizedIds;
    CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
        categorizedIds.insert(r.fileId128);
    });

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
        CategoryCacheManager::instance().forEachRecord([&](const CategoryItemRecord& r) {
            categorizedIds.insert(r.fileId128);
        });
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
