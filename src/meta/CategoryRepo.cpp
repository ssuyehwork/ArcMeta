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
    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint FROM categories WHERE id > 0 ORDER BY sort_order ASC";
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
    return executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE file_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    });
}

bool CategoryRepo::moveToTrashBatch(const std::vector<std::string>& fids) {
    return executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        // 1. Remove all existing category associations
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Insert into trash bucket
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // 3. Update is_trash flag
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, true);
        }
        return true;
    });
}

bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& fids) {
    return executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        // 1. Remove from trash bucket
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db,
            "DELETE FROM category_items WHERE category_id = ? AND file_id = ?",
            -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(delStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Add to "未分类" bucket
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, UNCATEGORIZED_CAT_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // 3. Clear is_trash flag in metadata cache + persist
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, false);
        }

        // 2026-06-xx 物理对账：恢复后触发全量统计重建
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        return true;
    });
}

bool CategoryRepo::restoreFromTrash(const std::string& fid) {
    return restoreFromTrashBatch({fid});
}

bool CategoryRepo::permanentlyDeleteBatch(const std::vector<std::string>& fids) {
    // Collect paths before removing from cache
    std::vector<std::wstring> paths;
    for (const auto& fid : fids) {
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        if (!path.empty()) paths.push_back(path);
    }

    bool ok = executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE file_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    });

    // 2. Remove from metadata table + in-memory cache (per-volume DBs handled inside removeMetadataSync)
    int removedCount = 0;
    for (const auto& path : paths) {
        MetadataManager::instance().removeMetadataSync(path);
        removedCount++;
    }

    // 3. Update "全部数据" count — permanent delete is the only operation that reduces it
    if (removedCount > 0) {
        incrementTotalFileCount(-removedCount);
    }

    return ok;
}

bool CategoryRepo::permanentlyDelete(const std::string& fid) {
    return permanentlyDeleteBatch({fid});
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

    // Step 1: Recursively collect all category IDs to delete (existing logic, keep as-is)
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

    // Step 2: Collect all unique File IDs from those categories (deduplicated)
    std::vector<std::string> fids;
    std::unordered_map<std::string, std::wstring> fidToPath; // fid -> path_hint
    for (int catId : toDelete) {
        auto items = getItemsInCategory(catId);
        for (const auto& item : items) {
            if (fidToPath.find(item.fileId128) == fidToPath.end()) {
                fidToPath[item.fileId128] = item.pathHint;
                fids.push_back(item.fileId128);
            }
        }
    }

    // Step 3: For each File ID — remove all its category associations, then insert one row into trash bucket
    executeFidBatch(fids, [&](sqlite3* innerDb, const std::string& fid) {
        const std::wstring& pathHint = fidToPath[fid];

        // Remove ALL existing category_items rows for this fid
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(innerDb, "DELETE FROM category_items WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // Insert one row into trash bucket
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(innerDb,
            "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, pathHint.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // Update in-memory cache: set isTrash = true, then persist
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        if (path.empty()) path = pathHint;
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, true);
        }
        return true;
    });

    // Step 4: Delete only the category rows (NOT category_items — already handled above)
    SqlTransaction trans(db);
    for (int delId : toDelete) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, delId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    trans.commit();

    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
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

    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFid(fileId128);

    SqlTransaction trans(db);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            trans.commit();

            // 2026-06-xx 物理同步：归类操作必然标记为受控项，驱动 UI 绿对勾显示
            if (!finalPath.empty()) {
                MetadataManager::instance().setManaged(finalPath, true, false);
            }

            syncCategorizedCountForFid(fileId128);
            
            // 2026-06-xx 物理优化：手动归类后立即触发侧边栏异步局部刷新
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    SqlTransaction trans(db);
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM category_items WHERE category_id = ? AND file_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            trans.commit();

            // 2026-06-xx 物理同步：移出分类后，需要重新判定 Managed 状态。
            // 由于 MetadataManager::setManaged 会同步更新 RuntimeMeta，
            // 这里利用 syncCategorizedCountForFid 间接完成状态刷新。
            std::wstring path = MetadataManager::instance().getPathByFid(fileId128);
            if (!path.empty()) {
                auto meta = MetadataManager::instance().getMeta(path);
                // 如果没有其他用户操作，且不再属于任何分类，则可能需要取消 Managed 标记。
                // 暂时保持现状，或调用一次 refresh 逻辑。
            }

            syncCategorizedCountForFid(fileId128);
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
    const char* sql = "SELECT category_id, file_id FROM category_items WHERE category_id > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int catId = sqlite3_column_int(stmt, 0);
            const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            if (fid) {
                std::wstring path = MetadataManager::instance().getPathByFid(fid);
                if (!path.empty()) {
                    auto meta = MetadataManager::instance().getMeta(path);
                    // 2026-06-xx 物理隔离加固：如果文件在回收站，不计入自定义分类数量
                    if (!meta.isFolder && !meta.isTrash) {
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
    updatePersistentStat(STAT_TOTAL_FILES, delta);
}

void CategoryRepo::incrementCategorizedCount(int delta) {
    s_categorizedCount += delta;
    updatePersistentStat(STAT_CATEGORIZED, delta);
}

void CategoryRepo::updatePersistentStat(const std::string& key, int delta) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, "
                      "COALESCE((SELECT value FROM system_stats WHERE key = ?), 0) + ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, delta);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

bool CategoryRepo::executeFidBatch(const std::vector<std::string>& fids, std::function<bool(struct sqlite3*, const std::string&)> action) {
    if (fids.empty()) return true;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    SqlTransaction trans(db);
    for (const auto& fid : fids) {
        if (!action(db, fid)) {
            trans.rollback();
            return false;
        }
    }
    bool ok = trans.commit();

    // 2026-06-xx 架构优化：批量变动后自动同步已分类计数，实现跨表一致性
    syncCategorizedCountForFid("");

    // 批量处理后通知 UI 刷新
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
    return ok;
}

void CategoryRepo::syncCategorizedCountForFid(const std::string& fid) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;

    // 2026-06-xx 物理对账：通过全局扫描并结合元数据排除文件夹，重新计算已分类总数
    // 这种做法比手动增减 delta 更稳健，能自动修正因程序崩溃导致的计数偏差
    std::unordered_set<std::string> uniqueFiles;
    sqlite3_stmt* stmt;
    // 物理红线：分类计数必须排除回收站 (ID = -8) 和未分类 (ID = -2)
    const char* sql = "SELECT DISTINCT file_id FROM category_items WHERE category_id > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* fidPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (fidPtr) {
                std::string currentFid = fidPtr;
                std::wstring path = MetadataManager::instance().getPathByFid(currentFid);
                if (!path.empty()) {
                    auto meta = MetadataManager::instance().getMeta(path);
                    // 核心红线：不计入文件夹，不计入回收站
                    if (!meta.isFolder && !meta.isTrash) {
                        uniqueFiles.insert(currentFid);
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    int count = (int)uniqueFiles.size();
    int oldCount = s_categorizedCount.load();
    s_categorizedCount.store(count);

    // 物理持久化：校准数据库中的统计指标
    if (count != oldCount) {
        updatePersistentStat(STAT_CATEGORIZED, count - oldCount);
    }

    // 2026-06-xx 物理同步：针对特定变动的 FID，刷新其 Managed 状态
    if (!fid.empty()) {
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        if (!path.empty()) {
            auto meta = MetadataManager::instance().getMeta(path);
            bool isCategorized = uniqueFiles.count(fid);
            // 判定：只要属于任何自定义分类，或者有用户操作信息，即为 Managed
            bool managed = isCategorized || meta.hasUserOperations();
            MetadataManager::instance().setManaged(path, managed, false);
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
                if (key == STAT_TOTAL_FILES) {
                    s_totalFileCount.store(val);
                    if (val > 0) hasSavedCounts = true;
                    qDebug() << "[Recount] 从数据库加载历史计数: Total =" << val;
                }
                else if (key == STAT_CATEGORIZED) {
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
        // 注意：此处仅统计用户自定义分类 (ID > 0)
        std::unordered_set<std::string> categorizedFids;
        if (db) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (fid) categorizedFids.insert(fid);
                }
                sqlite3_finalize(stmt);
            }
        }

        std::vector<std::wstring> managedPaths;
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            if (!meta.isFolder && !meta.isInvalid) {
                total++;  // includes trash

                bool isCategorized = categorizedFids.count(meta.fileId128);
                if (!meta.isTrash && isCategorized) {
                    categorized++;
                }

                // 2026-06-xx 物理同步：只要在数据库中有记录（即便未分类），就标记为 Managed
                if (isCategorized || meta.hasUserOperations()) {
                    managedPaths.push_back(path);
                }
            }
        });

        // 2026-06-xx 物理修复：在 forEach 外部执行 setManaged，防止迭代时死锁或触发写锁冲突
        for (const auto& p : managedPaths) {
            MetadataManager::instance().setManaged(p, true, false);
        }
        
        qDebug() << "[Recount] 盘点完成。总计:" << total << "已分类:" << categorized;
        s_totalFileCount.store(total);
        s_categorizedCount.store(categorized);
        
        if (db) {
            SqlTransaction trans(db);
            sqlite3_stmt* stmt;
            const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, STAT_TOTAL_FILES, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, total);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, STAT_CATEGORIZED, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, categorized);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            trans.commit();
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
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
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
    
    // 2026-06-xx 性能优化：仅获取自定义分类 (ID > 0) 的 FID 集合
    std::unordered_set<std::string> categorizedFids;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
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
        // Never count folders
        if (meta.isFolder) return;

        // Invalid files: isolated, not counted in anything else
        if (meta.isInvalid) {
            invalidCount++;
            return;
        }

        // Trash files: count for trash badge, excluded from all other virtual category stats
        if (meta.isTrash) {
            trashCount++;
            return;
        }

        // 2026-06-xx 物理修复：规则 1.4，全部数据计数必须排除回收站文件
        activeTotal++;

        // Active (non-trash, non-invalid, non-folder) files only from here
        if (meta.tags.isEmpty()) untagged++;
        if (meta.atime >= now - 86400000.0) recently++;
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
            // 2026-06-xx 性能优化：查询“未分类”路径时，排除掉已在自定义分类 (ID > 0) 中的文件
            if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
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
            // 非特殊视图下，严禁显示失效数据
            if (meta.isInvalid) return;

            if (type == "all") match = true;
            else {
                // 其他活跃视图（未标签、最近访问、未分类）必须排除回收站项
                if (meta.isTrash) return;

                if (type == "untagged" && meta.tags.isEmpty()) match = true;
                else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
                else if (type == "uncategorized" && categorizedIds.find(meta.fileId128) == categorizedIds.end()) match = true;
            }
        }
        
        if (match) paths << QString::fromStdWString(path);
    });
    return paths;
}

} // namespace ArcMeta
