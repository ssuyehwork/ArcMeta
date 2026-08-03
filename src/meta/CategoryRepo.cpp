#include "CategoryRepo.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "sqlite3.h"
#include "../core/AppConfig.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtConcurrent>
#include <set>
#include <unordered_set>
#include <algorithm>

namespace ArcMeta {

std::atomic<int> CategoryRepo::s_totalFileCount{0};
std::atomic<int> CategoryRepo::s_categorizedCount{0};
std::atomic<bool> CategoryRepo::s_countsDirty{true};

std::atomic<int> CategoryRepo::s_totalCount{0};
std::atomic<int> CategoryRepo::s_tagsCount{0};
std::atomic<int> CategoryRepo::s_recentlyVisitedCount{0};
std::atomic<int> CategoryRepo::s_untaggedCount{0};
std::atomic<int> CategoryRepo::s_uncategorizedCount{0};
std::atomic<int> CategoryRepo::s_trashCount{0};

std::mutex CategoryRepo::s_tagsMutex;
QSet<QString> CategoryRepo::s_globalTagsSet;


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
    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon FROM categories WHERE id > 0 ORDER BY sort_order ASC";
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
            c.physicalFrn = sqlite3_column_int64(stmt, 9);
            const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 10));
            if (wpath) c.physicalPath = wpath;
            const wchar_t* wicon = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 11));
            if (wicon) c.icon = wicon;
            results.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

bool CategoryRepo::add(Category& cat) {
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    if (dbs.empty()) return false;

    sqlite3* mainDb = DatabaseManager::instance().getGlobalDb();
    if (!mainDb) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(mainDb, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
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
        sqlite3_bind_int64(stmt, 9, cat.physicalFrn);
        sqlite3_bind_text16(stmt, 10, cat.physicalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 11, cat.icon.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            cat.id = static_cast<int>(sqlite3_last_insert_rowid(mainDb));
            sqlite3_finalize(stmt);

            // Now write to all OTHER active databases with the same explicit ID!
            const char* sqlWithId = "INSERT OR REPLACE INTO categories (id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            for (sqlite3* db : dbs) {
                if (db == mainDb) continue;
                sqlite3_stmt* stmtOther;
                if (sqlite3_prepare_v2(db, sqlWithId, -1, &stmtOther, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmtOther, 1, cat.id);
                    sqlite3_bind_int(stmtOther, 2, cat.parentId);
                    sqlite3_bind_text16(stmtOther, 3, cat.name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(stmtOther, 4, cat.color.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(stmtOther, 5, tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmtOther, 6, cat.sortOrder);
                    sqlite3_bind_int(stmtOther, 7, cat.pinned ? 1 : 0);
                    sqlite3_bind_int(stmtOther, 8, cat.encrypted ? 1 : 0);
                    sqlite3_bind_text16(stmtOther, 9, cat.encryptHint.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmtOther, 10, cat.physicalFrn);
                    sqlite3_bind_text16(stmtOther, 11, cat.physicalPath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(stmtOther, 12, cat.icon.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmtOther);
                    sqlite3_finalize(stmtOther);
                }
            }
            s_countsDirty.store(true);
            qDebug() << "[CategoryRepo] add success across dbs: Name =" << QString::fromStdWString(cat.name) << "ID =" << cat.id << "Parent =" << cat.parentId;
            return true;
        } else {
            qDebug() << "[CategoryRepo] add FAILED during step:" << sqlite3_errmsg(mainDb) << "Code:" << rc;
        }
        sqlite3_finalize(stmt);
    } else {
        qDebug() << "[CategoryRepo] add FAILED during prepare:" << sqlite3_errmsg(mainDb) << "Code:" << rc;
    }
    return false;
}

bool CategoryRepo::removeAllCategories(const std::string& folderId) {
    return removeAllCategoriesBatch({folderId});
}

bool CategoryRepo::removeAllCategoriesBatch(const std::vector<std::string>& folderIds) {
    int categorizedDelta = 0;
    for (const auto& fid : folderIds) {
        if (!getItemCategoryIds(fid).empty()) {
            s_uncategorizedCount.fetch_add(1);
            s_categorizedCount.fetch_sub(1);
            categorizedDelta--;
        }
    }
    if (categorizedDelta != 0) {
        updatePersistentStat(STAT_CATEGORIZED, categorizedDelta);
    }
    return executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return true;
    });
}

std::vector<int> CategoryRepo::getItemCategoryIds(const std::string& folderId, const std::wstring& pathHint) {
    std::vector<int> ids;
    if (folderId.empty()) return ids;
    std::wstring path = pathHint;
    if (path.empty()) {
        path = MetadataManager::instance().getPathByFolderId(folderId);
    }
    sqlite3* db = DatabaseManager::instance().getDbForPath(path);
    if (!db) return ids;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT category_id FROM category_items WHERE folder_id = ? AND category_id > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, folderId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ids.push_back(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return ids;
}

bool CategoryRepo::moveToTrashBatch(const std::vector<std::string>& folderIds) {
    return executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        // 1. Remove all existing category associations
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Insert into trash bucket
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
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

bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& folderIds) {
    return executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        // 1. Remove from trash bucket
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db,
            "DELETE FROM category_items WHERE category_id = ? AND folder_id = ?",
            -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(delStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Add to "未分类" bucket
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
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

bool CategoryRepo::restoreFromTrash(const std::string& folderId) {
    return restoreFromTrashBatch({folderId});
}

bool CategoryRepo::permanentlyDeleteBatch(const std::vector<std::string>& folderIds) {
    // Collect paths before removing from cache
    std::vector<std::wstring> paths;
    for (const auto& fid : folderIds) {
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        if (!path.empty()) paths.push_back(path);
    }

    bool ok = executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
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
        s_trashCount.fetch_sub(removedCount);
        updatePersistentStat("sys_trash_count", -removedCount);
    }

    return ok;
}

bool CategoryRepo::permanentlyDelete(const std::string& folderId) {
    return permanentlyDeleteBatch({folderId});
}

Category CategoryRepo::getById(int id) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    Category c;
    if (!db) return c;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon FROM categories WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
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
            c.physicalFrn = sqlite3_column_int64(stmt, 9);
            const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 10));
            if (wpath) c.physicalPath = wpath;
            const wchar_t* wicon = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 11));
            if (wicon) c.icon = wicon;
        }
        sqlite3_finalize(stmt);
    }
    return c;
}

bool CategoryRepo::update(const Category& cat) {
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    const char* sql = "UPDATE categories SET parent_id=?, name=?, color=?, preset_tags=?, sort_order=?, pinned=?, encrypted=?, encrypt_hint=?, physical_frn=?, physical_path=?, icon=? WHERE id=?";
    
    bool anyOk = false;
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt;
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
            sqlite3_bind_int64(stmt, 9, cat.physicalFrn);
            sqlite3_bind_text16(stmt, 10, cat.physicalPath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 11, cat.icon.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 12, cat.id);

            if (sqlite3_step(stmt) == SQLITE_DONE) anyOk = true;
            sqlite3_finalize(stmt);
        }
    }
    if (anyOk) {
        s_countsDirty.store(true);
    }
    return anyOk;
}

int CategoryRepo::findByFrn(uint64_t frn) {
    if (frn == 0) return 0;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return 0;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM categories WHERE physical_frn = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, frn);
        int id = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return id;
    }
    return 0;
}

bool CategoryRepo::updatePhysicalMapping(int id, uint64_t frn, const std::wstring& path) {
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    const char* sql = "UPDATE categories SET physical_frn = ?, physical_path = ? WHERE id = ?";
    
    bool anyOk = false;
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, frn);
            sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, id);
            if (sqlite3_step(stmt) == SQLITE_DONE) anyOk = true;
            sqlite3_finalize(stmt);
        }
    }
    return anyOk;
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
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        if (id > 0) {
            qDebug() << "[CategoryRepo] findCategoryId found:" << QString::fromStdWString(name) << "->" << id << "under parent" << parentId;
        }
        return id;
    }
    return 0;
}

bool CategoryRepo::remove(int id) {
    sqlite3* mainDb = DatabaseManager::instance().getGlobalDb();
    if (!mainDb) return false;

    // Step 1: Recursively collect all category IDs to delete (using mainDb as source of truth for tree structure)
    std::vector<int> toDelete = {id};
    size_t i = 0;
    while (i < toDelete.size()) {
        int pid = toDelete[i++];
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(mainDb, "SELECT id FROM categories WHERE parent_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, pid);
            while (sqlite3_step(stmt) == SQLITE_ROW) toDelete.push_back(sqlite3_column_int(stmt, 0));
            sqlite3_finalize(stmt);
        }
    }

    // Step 2: Collect all unique Folder IDs from those categories (deduplicated)
    std::vector<std::string> fids;
    std::unordered_map<std::string, std::wstring> fidToPath; // fid -> path_hint
    for (int catId : toDelete) {
        auto items = getItemsInCategory(catId);
        for (const auto& item : items) {
            if (fidToPath.find(item.folderId) == fidToPath.end()) {
                fidToPath[item.folderId] = item.pathHint;
                fids.push_back(item.folderId);
            }
        }
    }

    // Step 3: For each Folder ID — remove all its category associations, then insert one row into trash bucket
    executeFidBatch(fids, [&](sqlite3* innerDb, const std::string& fid) {
        const std::wstring& pathHint = fidToPath[fid];

        // Remove ALL existing category_items rows for this fid
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(innerDb, "DELETE FROM category_items WHERE folder_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // Insert one row into trash bucket
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(innerDb,
            "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, pathHint.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // Update in-memory cache: set isTrash = true, then persist
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        if (path.empty()) path = pathHint;
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, true);
        }
        return true;
    });

    // 收集所有关联了物理路径的文件夹分类
    std::vector<std::wstring> physicalDirsToDelete;
    for (int delId : toDelete) {
        Category cat = getById(delId);
        if (cat.id > 0 && !cat.physicalPath.empty()) {
            physicalDirsToDelete.push_back(cat.physicalPath);
        }
    }

    // 4. Delete the category rows and associations from ALL active databases
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        SqlTransaction trans(db);
        for (int delId : toDelete) {
            // 首先清理子项关联，防止幽灵关联
            sqlite3_stmt* itemDelStmt;
            if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE category_id = ?", -1, &itemDelStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(itemDelStmt, 1, delId);
                sqlite3_step(itemDelStmt);
                sqlite3_finalize(itemDelStmt);
            }

            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, delId);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        trans.commit();
    }

    s_countsDirty.store(true);
    // ✅ 修正后：删除分类只清理数据库与内存关联，严禁物理删除用户磁盘上的实际文件夹！
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

bool CategoryRepo::updateCategoryColorByPath(const std::wstring& path, const std::wstring& color) {
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return false;

    const char* sql = "UPDATE categories SET color = ? WHERE physical_path = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, color.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (ok) {
            qDebug() << "[DB_TRACE] updateCategoryColorByPath 成功同步更新 categories 表分类颜色，路径:" << QString::fromStdWString(path) << "颜色:" << QString::fromStdWString(color);
            DatabaseManager::instance().flushAll();
            return true;
        }
    }
    qWarning() << "[DB_TRACE] updateCategoryColorByPath 执行失败！路径:" << QString::fromStdWString(path);
    return false;
}

bool CategoryRepo::renamePhysicalCategoryPath(const std::wstring& oldPath, const std::wstring& newPath) {
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return false;

    bool anyOk = false;

    // 1. 更新 categories 表中的 physical_path 物理匹配
    // 同时也需要迁移作为其子目录的 1:1 物理镜像分支的前缀：
    // 例如 D:\projects 重命名为 D:\projects_new 时，D:\projects\cpp 应该变为 D:\projects_new\cpp
    std::wstring oldPathWithSlash = oldPath + L"\\";
    std::wstring newPathWithSlash = newPath + L"\\";

    // 更新精准等于它的 physical_path
    {
        const char* sql = "UPDATE categories SET physical_path = ? WHERE physical_path = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(memDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmt, 1, newPath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 2, oldPath.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) anyOk = true;
            sqlite3_finalize(stmt);
        }
    }

    // 更新以此作为子路径前缀的所有 categories 项
    {
        // 查找所有 categories 并迁移子物理目录映射项
        const char* sqlSel = "SELECT id, physical_path FROM categories WHERE physical_path LIKE ?";
        sqlite3_stmt* stmtSel = nullptr;
        std::wstring matchPattern = oldPath + L"\\%";
        if (sqlite3_prepare_v2(memDb, sqlSel, -1, &stmtSel, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmtSel, 1, matchPattern.c_str(), -1, SQLITE_TRANSIENT);
            std::vector<std::pair<int, std::wstring>> listToUpdate;
            while (sqlite3_step(stmtSel) == SQLITE_ROW) {
                int cid = sqlite3_column_int(stmtSel, 0);
                const wchar_t* pText = (const wchar_t*)sqlite3_column_text16(stmtSel, 1);
                if (pText) {
                    listToUpdate.push_back({cid, pText});
                }
            }
            sqlite3_finalize(stmtSel);

            const char* sqlUpd = "UPDATE categories SET physical_path = ? WHERE id = ?";
            for (const auto& pair : listToUpdate) {
                std::wstring subPath = pair.second;
                if (subPath.rfind(oldPathWithSlash, 0) == 0) { // startsWith
                    std::wstring subNewPath = newPathWithSlash + subPath.substr(oldPathWithSlash.length());
                    sqlite3_stmt* stmtUpd = nullptr;
                    if (sqlite3_prepare_v2(memDb, sqlUpd, -1, &stmtUpd, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text16(stmtUpd, 1, subNewPath.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmtUpd, 2, pair.first);
                        sqlite3_step(stmtUpd);
                        sqlite3_finalize(stmtUpd);
                        anyOk = true;
                    }
                }
            }
        }
    }

    // 2. 更新 category_items 中的 path_hint 指针，防止断开关联
    {
        // 精准等于
        {
            const char* sql = "UPDATE category_items SET path_hint = ? WHERE path_hint = ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(memDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text16(stmt, 1, newPath.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 2, oldPath.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_DONE) anyOk = true;
                sqlite3_finalize(stmt);
            }
        }

        // 以 oldPath 为前缀的子项 path_hint 重写
        {
            const char* sqlSel = "SELECT rowid, path_hint FROM category_items WHERE path_hint LIKE ?";
            sqlite3_stmt* stmtSel = nullptr;
            std::wstring matchPattern = oldPath + L"\\%";
            if (sqlite3_prepare_v2(memDb, sqlSel, -1, &stmtSel, nullptr) == SQLITE_OK) {
                sqlite3_bind_text16(stmtSel, 1, matchPattern.c_str(), -1, SQLITE_TRANSIENT);
                std::vector<std::pair<long long, std::wstring>> itemsToUpdate;
                while (sqlite3_step(stmtSel) == SQLITE_ROW) {
                    long long rowid = sqlite3_column_int64(stmtSel, 0);
                    const wchar_t* pText = (const wchar_t*)sqlite3_column_text16(stmtSel, 1);
                    if (pText) {
                        itemsToUpdate.push_back({rowid, pText});
                    }
                }
                sqlite3_finalize(stmtSel);

                const char* sqlUpd = "UPDATE category_items SET path_hint = ? WHERE rowid = ?";
                for (const auto& pair : itemsToUpdate) {
                    std::wstring subPath = pair.second;
                    if (subPath.rfind(oldPathWithSlash, 0) == 0) { // startsWith
                        std::wstring subNewPath = newPathWithSlash + subPath.substr(oldPathWithSlash.length());
                        sqlite3_stmt* stmtUpd = nullptr;
                        if (sqlite3_prepare_v2(memDb, sqlUpd, -1, &stmtUpd, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text16(stmtUpd, 1, subNewPath.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int64(stmtUpd, 2, pair.first);
                            sqlite3_step(stmtUpd);
                            sqlite3_finalize(stmtUpd);
                            anyOk = true;
                        }
                    }
                }
            }
        }
    }

    if (anyOk) {
        DatabaseManager::instance().flushAll();
    }
    return anyOk;
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& folderId, const std::wstring& pathHint) {
    WriteGuard guard;
    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFolderId(folderId);

    sqlite3* memDb = DatabaseManager::instance().getDbForPath(finalPath);
    if (!memDb) return false;

    const char* sql = "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
    double addedAt = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(memStmt, 1, categoryId);
        sqlite3_bind_text(memStmt, 2, folderId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(memStmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(memStmt, 4, addedAt);
        
        if (sqlite3_step(memStmt) == SQLITE_DONE) {
            sqlite3_finalize(memStmt);

            // 如果之前未分类，增加后变成有分类，则减去 uncategorizedCount，增加 categorizedCount 并持久化
            if (getItemCategoryIds(folderId, finalPath).size() == 1) {
                s_uncategorizedCount.fetch_sub(1);
                s_categorizedCount.fetch_add(1);
                updatePersistentStat(STAT_CATEGORIZED, 1);
            }

            // 归类操作不应直接触发表入库，应由物理位移（如迁移）后再由 AutoImportManager 驱动。

            s_countsDirty.store(true);
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
            return true;
        }
        sqlite3_finalize(memStmt);
    }
    return false;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& folderId) {
    WriteGuard guard;
    std::wstring path = MetadataManager::instance().getPathByFolderId(folderId);
    sqlite3* memDb = DatabaseManager::instance().getDbForPath(path);
    if (!memDb) return false;

    const char* sql = "DELETE FROM category_items WHERE category_id = ? AND folder_id = ?";
    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(memStmt, 1, categoryId);
        sqlite3_bind_text(memStmt, 2, folderId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(memStmt) == SQLITE_DONE) {
            sqlite3_finalize(memStmt);

            // 如果移除后不再有任何分类，则增加 uncategorizedCount，减少 categorizedCount 并持久化
            if (getItemCategoryIds(folderId, path).empty()) {
                s_uncategorizedCount.fetch_add(1);
                s_categorizedCount.fetch_sub(1);
                updatePersistentStat(STAT_CATEGORIZED, -1);
            }

            s_countsDirty.store(true);
            return true;
        }
        sqlite3_finalize(memStmt);
    }
    return false;
}

std::vector<CategoryItem> CategoryRepo::getItemsInCategory(int categoryId) {
    return getItemsInCategories({categoryId});
}

std::vector<CategoryItem> CategoryRepo::getItemsInCategories(const std::vector<int>& categoryIds) {
    std::vector<CategoryItem> results;
    if (categoryIds.empty()) return results;

    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    QStringList placeholders;
    for (int i = 0; i < categoryIds.size(); ++i) placeholders << "?";
    QString sql = QString("SELECT DISTINCT folder_id, path_hint FROM category_items WHERE category_id IN (%1)").arg(placeholders.join(","));

    std::map<std::string, std::wstring> uniqueItems;

    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            for (int i = 0; i < categoryIds.size(); ++i) {
                sqlite3_bind_int(stmt, i + 1, categoryIds[i]);
            }
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const wchar_t* path = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                if (fid) {
                    uniqueItems[fid] = path ? path : L"";
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    for (const auto& [fid, path] : uniqueItems) {
        results.push_back({fid, path});
    }
    return results;
}

std::vector<int> CategoryRepo::getSubtreeIds(int categoryId) {
    std::vector<int> ids = {categoryId};
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return ids;

    size_t i = 0;
    while (i < ids.size()) {
        int pid = ids[i++];
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE parent_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, pid);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int childId = sqlite3_column_int(stmt, 0);
                if (std::find(ids.begin(), ids.end(), childId) == ids.end()) {
                    ids.push_back(childId);
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    return ids;
}

std::vector<CategoryItem> CategoryRepo::getItemsRecursive(int categoryId) {
    std::vector<int> ids = getSubtreeIds(categoryId);

    std::map<std::string, std::wstring> resultsMap;
    for (int cid : ids) {
        auto items = getItemsInCategory(cid);
        for (const auto& item : items) resultsMap[item.folderId] = item.pathHint;
    }

    std::vector<CategoryItem> results;
    for (auto const& [fid, path] : resultsMap) results.push_back({fid, path});
    return results;
}

std::vector<std::string> CategoryRepo::getFolderIdsInCategory(int categoryId) {
    auto items = getItemsInCategory(categoryId);
    std::vector<std::string> res;
    for (const auto& i : items) res.push_back(i.folderId);
    return res;
}

std::vector<std::string> CategoryRepo::getFolderIdsRecursive(int categoryId) {
    auto items = getItemsRecursive(categoryId);
    std::vector<std::string> res;
    for (const auto& i : items) res.push_back(i.folderId);
    return res;
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() { 
    static std::mutex countsMutex;
    static std::vector<std::pair<int, int>> cachedCounts;

    std::lock_guard<std::mutex> lock(countsMutex);
    if (!s_countsDirty.load()) {
        return cachedCounts;
    }

    std::vector<std::pair<int, int>> res; 
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs(); 
    std::map<int, std::unordered_set<std::string>> catToUniqueFids; 
 
    for (sqlite3* db : dbs) { 
        sqlite3_stmt* stmt; 
        const char* sql = "SELECT folder_id, category_id FROM category_items "
                          "WHERE category_id > 0 AND category_id NOT IN "
                          "(SELECT id FROM categories WHERE parent_id = 0 AND name LIKE 'ArcMeta.Library_%')";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) { 
            while (sqlite3_step(stmt) == SQLITE_ROW) { 
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)); 
                int catId = sqlite3_column_int(stmt, 1); 
                if (fid) catToUniqueFids[catId].insert(fid); 
            } 
            sqlite3_finalize(stmt); 
        } 
    } 
 
    for (auto const& [id, fids] : catToUniqueFids) { 
        res.push_back({id, static_cast<int>(fids.size())}); 
    } 
    cachedCounts = res;
    s_countsDirty.store(false);
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
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, "
                      "COALESCE((SELECT value FROM system_stats WHERE key = ?), 0) + ?)";
    
    for (sqlite3* memDb : dbs) {
        sqlite3_stmt* memStmt;
        if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(memStmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(memStmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(memStmt, 3, delta);
            sqlite3_step(memStmt);
            sqlite3_finalize(memStmt);
        }
    }
}

bool CategoryRepo::executeFidBatch(const std::vector<std::string>& folderIds, std::function<bool(struct sqlite3*, const std::string&)> action) {
    if (folderIds.empty()) return true;

    // Group folderIds by their corresponding database connection
    std::map<sqlite3*, std::vector<std::string>> dbToFids;
    for (const auto& fid : folderIds) {
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        sqlite3* db = DatabaseManager::instance().getDbForPath(path);
        if (db) {
            dbToFids[db].push_back(fid);
        }
    }

    bool allOk = true;
    for (auto& [db, fidsInDb] : dbToFids) {
        SqlTransaction trans(db);
        bool transOk = true;
        for (const auto& fid : fidsInDb) {
            if (!action(db, fid)) {
                transOk = false;
                break;
            }
        }
        if (transOk) {
            trans.commit();
        } else {
            trans.rollback();
            allOk = false;
        }
    }

    s_countsDirty.store(true);
    // 批量处理后通知 UI 刷新
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
    return allOk;
}

void CategoryRepo::syncCategorizedCountForFid(const std::string& /*folderId*/) {
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    std::unordered_set<std::string> uniqueFids;

    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt;
        const char* sql = "SELECT DISTINCT folder_id FROM category_items "
                          "WHERE category_id > 0 AND category_id NOT IN "
                          "(SELECT id FROM categories WHERE parent_id = 0 AND name LIKE 'ArcMeta.Library_%')";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fidPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (fidPtr) {
                    uniqueFids.insert(fidPtr);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    int count = static_cast<int>(uniqueFids.size());
    int oldCount = s_categorizedCount.load();
    s_categorizedCount.store(count);
    
    // 物理持久化：直接更新增量
    if (count != oldCount) {
        updatePersistentStat(STAT_CATEGORIZED, count - oldCount);
    }
}

void CategoryRepo::loadStatsFromDb() {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT key, value FROM system_stats";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* keyPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (!keyPtr) continue;
            std::string key = keyPtr;
            int val = sqlite3_column_int(stmt, 1);

            if (key == STAT_TOTAL_FILES) s_totalFileCount.store(val);
            else if (key == STAT_CATEGORIZED) s_categorizedCount.store(val);
            else if (key == "sys_total_count") s_totalCount.store(val);
            else if (key == "sys_tags_count") s_tagsCount.store(val);
            else if (key == "sys_recently_visited_count") s_recentlyVisitedCount.store(val);
            else if (key == "sys_untagged_count") s_untaggedCount.store(val);
            else if (key == "sys_uncategorized_count") s_uncategorizedCount.store(val);
            else if (key == "sys_trash_count") s_trashCount.store(val);
        }
        sqlite3_finalize(stmt);
    }
}

void CategoryRepo::fullRecount() {
    // 🚨 彻底根除全量物理对账逻辑：该函数已被完全注销，拒绝一切对账、覆盖和物理脏写，保持系统极度清澈与极速
    qDebug() << "[Recount][CLEANUP] CategoryRepo::fullRecount has been completely removed. Skip recount.";
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
    res["all"] = s_totalCount.load();
    res["tags"] = s_tagsCount.load();
    res["recently_visited"] = s_recentlyVisitedCount.load();
    res["untagged"] = s_untaggedCount.load();
    res["uncategorized"] = s_uncategorizedCount.load();
    res["trash"] = s_trashCount.load();
    return res;
}

QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    std::unordered_set<std::string> categorizedIds;
    if (type == "uncategorized") {
        auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
        for (sqlite3* db : dbs) {
            sqlite3_stmt* stmt;
            // 2026-06-xx 性能优化：查询“未分类”路径时，排除掉已在自定义分类 (ID > 0) 中的文件
            const char* sql = "SELECT DISTINCT folder_id FROM category_items "
                              "WHERE category_id > 0 AND category_id NOT IN "
                              "(SELECT id FROM categories WHERE parent_id = 0 AND name LIKE 'ArcMeta.Library_%')";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
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
        std::wstring finalPath = path;

        if (type == "trash") {
            if (meta.isTrash) match = true;
        } else {
            if (type == "all") {
                if (meta.isTrash) return; 
                match = true;
            }
            else {
                if (meta.isTrash) return;

                if (type == "untagged" && meta.tags.isEmpty()) match = true;
                else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
                else if (type == "uncategorized" && !meta.folderId.empty() && categorizedIds.find(meta.folderId) == categorizedIds.end()) match = true;
            }
        }
        
        if (match && !finalPath.empty()) paths << QString::fromStdWString(finalPath);
    });
    return paths;
}

} // namespace ArcMeta
