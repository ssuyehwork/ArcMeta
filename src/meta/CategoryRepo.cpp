#include "CategoryRepo.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "sqlite3.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>
#include <set>
#include <unordered_set>
#include <algorithm>

namespace ArcMeta {

std::atomic<int> CategoryRepo::s_totalFileCount{0};
std::atomic<int> CategoryRepo::s_categorizedCount{0};


void CategoryRepo::initialize() {
    // SQLite 模式下，DatabaseManager::init() 已由 MetadataManager 调用
}

void CategoryRepo::saveImmediately() {
    DatabaseManager::instance().flushAll();
}

std::vector<Category> CategoryRepo::getAll() {
    std::vector<Category> results;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return results;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint FROM categories ORDER BY sort_order ASC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Category c;
            c.id = sqlite3_column_int(stmt, 0);
            c.parentId = sqlite3_column_int(stmt, 1);
            const wchar_t* wname = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
            if (wname) c.name = wname;
            const wchar_t* color = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 3));
            if (color) c.color = color;
            const wchar_t* wtags = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
            QString tags = wtags ? QString::fromWCharArray(wtags) : "";
            for (const auto& t : tags.split(",", Qt::SkipEmptyParts)) c.presetTags.push_back(t.toStdWString());
            c.sortOrder = sqlite3_column_int(stmt, 5);
            c.pinned = sqlite3_column_int(stmt, 6) != 0;
            c.encrypted = sqlite3_column_int(stmt, 7) != 0;
            const wchar_t* hint = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 8));
            if (hint) c.encryptHint = hint;
            results.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

bool CategoryRepo::add(Category& cat) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, cat.parentId);
        sqlite3_bind_text16(stmt, 2, cat.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, cat.color.c_str(), -1, SQLITE_TRANSIENT);
        
        QStringList tags;
        for (const auto& t : cat.presetTags) tags << QString::fromStdWString(t);
        sqlite3_bind_text16(stmt, 4, tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        
        sqlite3_bind_int(stmt, 5, cat.sortOrder);
        sqlite3_bind_int(stmt, 6, cat.pinned ? 1 : 0);
        sqlite3_bind_int(stmt, 7, cat.encrypted ? 1 : 0);
        sqlite3_bind_text16(stmt, 8, cat.encryptHint.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            cat.id = static_cast<int>(sqlite3_last_insert_rowid(db));
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool CategoryRepo::removeAllCategories(const std::string& fileId128) {
    return removeAllCategoriesBatch({fileId128});
}

bool CategoryRepo::removeAllCategoriesBatch(const std::vector<std::string>& fids) {
    if (fids.empty()) return true;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    int removedCategorizedCount = 0;
    
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    
    // 1. 预查这些 FID 中有多少是曾经被分类过的（去重统计）
    sqlite3_stmt* checkStmt;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM category_items WHERE file_id = ? LIMIT 1", -1, &checkStmt, nullptr) == SQLITE_OK) {
        for (const auto& fid : fids) {
            sqlite3_bind_text(checkStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) {
                removedCategorizedCount++;
            }
            sqlite3_reset(checkStmt);
        }
        sqlite3_finalize(checkStmt);
    }

    // 2. 物理根除关联
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM category_items WHERE file_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& fid : fids) {
            sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // 3. 更新已分类统计
    if (removedCategorizedCount > 0) {
        incrementCategorizedCount(-removedCategorizedCount);
    }
    
    return true;
}

bool CategoryRepo::update(const Category& cat) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "UPDATE categories SET parent_id=?, name=?, color=?, preset_tags=?, sort_order=?, pinned=?, encrypted=?, encrypt_hint=? WHERE id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, cat.parentId);
        sqlite3_bind_text16(stmt, 2, cat.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, cat.color.c_str(), -1, SQLITE_TRANSIENT);
        QStringList tags;
        for (const auto& t : cat.presetTags) tags << QString::fromStdWString(t);
        sqlite3_bind_text16(stmt, 4, tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, cat.sortOrder);
        sqlite3_bind_int(stmt, 6, cat.pinned ? 1 : 0);
        sqlite3_bind_int(stmt, 7, cat.encrypted ? 1 : 0);
        sqlite3_bind_text16(stmt, 8, cat.encryptHint.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, cat.id);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

int CategoryRepo::findCategoryId(int parentId, const std::wstring& name) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return 0;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM categories WHERE parent_id = ? AND name = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, parentId);
        sqlite3_bind_text16(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        int id = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return id;
    }
    return 0;
}

bool CategoryRepo::remove(int id) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    // 获取所有子分类 ID (递归删除)
    std::vector<int> toDelete = {id};
    size_t i = 0;
    while (i < toDelete.size()) {
        int pid = toDelete[i++];
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE parent_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, pid);
            while (sqlite3_step(stmt) == SQLITE_ROW) toDelete.push_back(sqlite3_column_int(stmt, 0));
            sqlite3_finalize(stmt);
        }
    }

    char* errMsg = nullptr;
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    for (int delId : toDelete) {
        std::string sqlCat = "DELETE FROM categories WHERE id = " + std::to_string(delId);
        std::string sqlItems = "DELETE FROM category_items WHERE category_id = " + std::to_string(delId);
        if (sqlite3_exec(db, sqlCat.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            qDebug() << "[CategoryRepo] Remove error (cat):" << errMsg;
            sqlite3_free(errMsg);
            errMsg = nullptr;
        }
        if (sqlite3_exec(db, sqlItems.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            qDebug() << "[CategoryRepo] Remove error (items):" << errMsg;
            sqlite3_free(errMsg);
            errMsg = nullptr;
        }
    }
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    return true;
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    auto cats = getAll();
    std::vector<Category*> targets;
    for (auto& c : cats) if (c.parentId == parentId) targets.push_back(&c);
    
    std::sort(targets.begin(), targets.end(), [ascending](Category* a, Category* b) {
        int cmp = a->name.compare(b->name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    for (size_t i = 0; i < targets.size(); ++i) {
        targets[i]->sortOrder = static_cast<int>(i);
        update(*targets[i]);
    }
    return true;
}

bool CategoryRepo::reorderAll(bool ascending) {
    auto cats = getAll();
    std::sort(cats.begin(), cats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    for (size_t i = 0; i < cats.size(); ++i) {
        cats[i].sortOrder = static_cast<int>(i);
        update(cats[i]);
    }
    return true;
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    // 检查是否已经存在于任何分类中（用于更新已分类计数）
    bool alreadyCategorized = false;
    sqlite3_stmt* checkStmt;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM category_items WHERE file_id = ? LIMIT 1", -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) alreadyCategorized = true;
        sqlite3_finalize(checkStmt);
    }

    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFid(fileId128);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            if (!alreadyCategorized) {
                incrementCategorizedCount(1);
            }
            sqlite3_finalize(stmt);
            
            // 2026-06-xx 物理优化：手动归类后立即触发侧边栏异步局部刷新
            emit MetadataManager::instance().metaChanged("__RELOAD_COUNT__");
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM category_items WHERE category_id = ? AND file_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            // 检查是否还有其他分类关联
            bool stillCategorized = false;
            sqlite3_stmt* checkStmt;
            if (sqlite3_prepare_v2(db, "SELECT 1 FROM category_items WHERE file_id = ? LIMIT 1", -1, &checkStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(checkStmt, 1, fileId128.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(checkStmt) == SQLITE_ROW) stillCategorized = true;
                sqlite3_finalize(checkStmt);
            }
            if (!stillCategorized) {
                incrementCategorizedCount(-1);
            }
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

std::vector<CategoryItem> CategoryRepo::getItemsInCategory(int categoryId) {
    std::vector<CategoryItem> results;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return results;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT file_id, path_hint FROM category_items WHERE category_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back({
                sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "",
                sqlite3_column_text16(stmt, 1) ? reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1)) : L""
            });
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

std::vector<CategoryItem> CategoryRepo::getItemsRecursive(int categoryId) {
    std::vector<int> ids = {categoryId};
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return {};

    size_t i = 0;
    while (i < ids.size()) {
        int pid = ids[i++];
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE parent_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, pid);
            while (sqlite3_step(stmt) == SQLITE_ROW) ids.push_back(sqlite3_column_int(stmt, 0));
            sqlite3_finalize(stmt);
        }
    }

    std::map<std::string, std::wstring> resultsMap;
    for (int cid : ids) {
        auto items = getItemsInCategory(cid);
        for (const auto& item : items) resultsMap[item.fileId128] = item.pathHint;
    }

    std::vector<CategoryItem> results;
    for (auto const& [fid, path] : resultsMap) results.push_back({fid, path});
    return results;
}

std::vector<std::string> CategoryRepo::getFileIdsInCategory(int categoryId) {
    auto items = getItemsInCategory(categoryId);
    std::vector<std::string> res;
    for (const auto& i : items) res.push_back(i.fileId128);
    return res;
}

std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) {
    auto items = getItemsRecursive(categoryId);
    std::vector<std::string> res;
    for (const auto& i : items) res.push_back(i.fileId128);
    return res;
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
    std::vector<std::pair<int, int>> res;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return res;

    // 2026-06-xx 按照用户要求：任何虚拟分类计数只可计数文件数量，绝不可包含文件夹数量
    // 逻辑：从关联表获取所有项，并结合 MetadataManager 剔除文件夹
    std::map<int, int> countMap;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT category_id, file_id FROM category_items";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int catId = sqlite3_column_int(stmt, 0);
            const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            if (fid) {
                std::wstring path = MetadataManager::instance().getPathByFid(fid);
                if (!path.empty()) {
                    auto meta = MetadataManager::instance().getMeta(path);
                    if (!meta.isFolder) {
                        countMap[catId]++;
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    for (auto const& [id, count] : countMap) {
        res.push_back({id, count});
    }
    return res;
}

int CategoryRepo::getTotalFileCount() {
    return s_totalFileCount.load();
}

int CategoryRepo::getUncategorizedCount() {
    return getSystemCounts()["uncategorized"];
}

void CategoryRepo::setTotalFileCount(int count) {
    s_totalFileCount.store(count);
}

void CategoryRepo::setCategorizedCount(int count) {
    s_categorizedCount.store(count);
}

void CategoryRepo::incrementTotalFileCount(int delta) {
    s_totalFileCount += delta;
    
    // 2026-06-xx 物理持久化：同步写入数据库
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES ('total_file_count', "
                          "COALESCE((SELECT value FROM system_stats WHERE key = 'total_file_count'), 0) + ?)";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, delta);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void CategoryRepo::incrementCategorizedCount(int delta) {
    s_categorizedCount += delta;
    
    // 2026-06-xx 物理持久化：同步写入数据库
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES ('categorized_count', "
                          "COALESCE((SELECT value FROM system_stats WHERE key = 'categorized_count'), 0) + ?)";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, delta);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void CategoryRepo::fullRecount() {
    // 2026-06-xx 按照用户要求：从数据库加载持久化的计数
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    bool hasSavedCounts = false;
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT key, value FROM system_stats", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* keyPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (!keyPtr) continue;
                std::string key = keyPtr;
                int val = sqlite3_column_int(stmt, 1);
                if (key == "total_file_count") {
                    s_totalFileCount.store(val);
                    if (val > 0) hasSavedCounts = true;
                    qDebug() << "[Recount] 从数据库加载历史计数: Total =" << val;
                }
                else if (key == "categorized_count") {
                    s_categorizedCount.store(val);
                    qDebug() << "[Recount] 从数据库加载历史计数: Categorized =" << val;
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2026-06-xx 物理修复：即便数据库中有记录，也在启动时执行一次全量内存核对以校准偏差。
    // 理由：防止因异常退出导致的 persistent count 与内存实际加载量不一致。
    // 如果 hasSavedCounts 为 false 或者数字异常，则必须强制重计并回写。
    if (!hasSavedCounts || s_totalFileCount.load() <= 0) {
        int total = 0;
        int categorized = 0;
        
        // 获取所有已分类的 FID 用于计数 (去重)
        std::unordered_set<std::string> categorizedFids;
        if (db) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (fid) categorizedFids.insert(fid);
                }
                sqlite3_finalize(stmt);
            }
        }

        MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
            if (!meta.isFolder && !meta.isTrash && !meta.isInvalid) {
                total++;
                if (categorizedFids.count(meta.fileId128)) categorized++;
            }
        });
        
        qDebug() << "[Recount] 盘点完成。总计:" << total << "已分类:" << categorized;
        s_totalFileCount.store(total);
        s_categorizedCount.store(categorized);
        
        if (db) {
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
            sqlite3_stmt* stmt;
            const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, "total_file_count", -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, total);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, "categorized_count", -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, categorized);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
            // 物理落盘，确保初始化结果即刻生效
            DatabaseManager::instance().flushAll();
        }
    }

    // 2026-06-xx 核心逻辑升级：物理有效性对账 (盘点 FRN)
    // 这一步在后台异步执行，验证文件是否被第三方删除。若失效，标记为 is_invalid 而非直接删除。
    // 使用 [db] 显式捕获数据库指针，并增加错误检查
    (void)QtConcurrent::run([db]() {
        if (!db) return;

        std::vector<std::pair<std::wstring, std::string>> itemsToCheck;
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            // 只对未标记失效且非回收站的文件进行物理校验
            if (!meta.isFolder && !meta.isInvalid && !meta.isTrash) {
                itemsToCheck.push_back({path, meta.fileId128});
            }
        });

        int invalidatedCount = 0;
        for (const auto& item : itemsToCheck) {
            std::string currentFid;
            // 通过 WinAPI 直接检查物理文件是否存在且 ID 匹配
            bool exists = MetadataManager::fetchWinApiMetadataDirect(item.first, currentFid);
            if (!exists || currentFid != item.second) {
                // 物理校验失败：文件可能已被第三方删除或移动，标记为失效
                invalidatedCount++;
                MetadataManager::instance().setInvalid(item.first, true);
            }
        }

        if (invalidatedCount > 0) {
            qDebug() << "[Recount] 物理校验发现" << invalidatedCount << "个失效项，已归类至失效数据";
            // 2026-06-xx 物理同步：强制将内存中的 is_invalid 变更刷入磁盘
            DatabaseManager::instance().flushAll();
        }
    });
}

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    std::vector<Category> results;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return results;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT c.id, c.parent_id, c.name, c.color, c.preset_tags, c.sort_order, c.pinned, c.encrypted, c.encrypt_hint "
                      "FROM categories c JOIN (SELECT category_id, MAX(added_at) as last_added FROM category_items GROUP BY category_id) r "
                      "ON c.id = r.category_id ORDER BY r.last_added DESC LIMIT ?";
                      
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Category c;
            c.id = sqlite3_column_int(stmt, 0);
            c.parentId = sqlite3_column_int(stmt, 1);
            const wchar_t* wname = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
            if (wname) c.name = wname;
            const wchar_t* color = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 3));
            if (color) c.color = color;
            const wchar_t* wtags = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
            QString tags = wtags ? QString::fromWCharArray(wtags) : "";
            for (const auto& t : tags.split(",", Qt::SkipEmptyParts)) c.presetTags.push_back(t.toStdWString());
            c.sortOrder = sqlite3_column_int(stmt, 5);
            c.pinned = sqlite3_column_int(stmt, 6) != 0;
            c.encrypted = sqlite3_column_int(stmt, 7) != 0;
            const wchar_t* hint = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 8));
            if (hint) c.encryptHint = hint;
            results.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

int CategoryRepo::getUniqueItemCount() {
    return s_totalFileCount.load();
}

int CategoryRepo::getUncategorizedItemCount() {
    return getSystemCounts()["uncategorized"];
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> res;
    
    // 2026-06-xx 性能优化：仅获取已分类 FID 集合，文件夹过滤统一推迟到元数据遍历阶段
    std::unordered_set<std::string> categorizedFids;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (fid) categorizedFids.insert(fid);
            }
            sqlite3_finalize(stmt);
        }
    }

    int activeTotal = 0, recently = 0, untagged = 0, uncategorized = 0, trashCount = 0, invalidCount = 0;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        // 核心红线：彻底排除文件夹计数
        if (meta.isFolder) return;

        if (meta.isInvalid) {
            invalidCount++;
            return; // 2026-06-xx 物理隔离：失效数据不参与其他正常分类统计
        }
        
        if (meta.isTrash) {
            trashCount++;
            return; // 2026-06-xx 物理隔离：回收站数据不参与“未分类”、“未标注”等统计
        }

        activeTotal++;

        // 2026-06-xx 按照用户要求：“未标签”与“已标签”相互隔离
        if (meta.tags.isEmpty()) untagged++;
        if (meta.atime >= now - 86400000.0) recently++;
        
        // “未分类”定义：存在于 metadata 表但不在 category_items 表中的有效文件（非回收站）
        if (categorizedFids.find(meta.fileId128) == categorizedFids.end()) {
            uncategorized++;
        }
    });

    res["all"] = activeTotal;
    res["recently_visited"] = recently;
    res["untagged"] = untagged;
    res["uncategorized"] = uncategorized;
    res["trash"] = trashCount;
    res["invalid_data"] = invalidCount;
    return res;
}

QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    std::unordered_set<std::string> categorizedIds;
    if (type == "uncategorized") {
        sqlite3* db = DatabaseManager::instance().getGlobalDb();
        if (db) {
            sqlite3_stmt* stmt;
            // 2026-06-xx 性能优化：查询“未分类”路径时，文件夹过滤由后续遍历逻辑统一处理
            if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (fid) categorizedIds.insert(fid);
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        // 核心红线：彻底排除文件夹
        if (meta.isFolder) return;
        
        bool match = false;
        if (type == "trash") {
            if (meta.isTrash) match = true;
        } else if (type == "invalid_data") {
            if (meta.isInvalid) match = true;
        } else {
            // 非特殊视图下，严禁显示失效数据和回收站数据
            if (meta.isInvalid || meta.isTrash) return;

            if (type == "all") match = true;
            else if (type == "untagged" && meta.tags.isEmpty()) match = true;
            else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
            else if (type == "uncategorized" && categorizedIds.find(meta.fileId128) == categorizedIds.end()) match = true;
        }
        
        if (match) paths << QString::fromStdWString(path);
    });
    return paths;
}

} // namespace ArcMeta
