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
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) {
        qDebug() << "[CategoryRepo] add FAILED: No DB connection";
        return false;
    }

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
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
            cat.id = static_cast<int>(sqlite3_last_insert_rowid(db));
            qDebug() << "[CategoryRepo] add success: Name =" << QString::fromStdWString(cat.name) << "ID =" << cat.id << "Parent =" << cat.parentId;
            sqlite3_finalize(stmt);
            return true;
        } else {
            qDebug() << "[CategoryRepo] add FAILED during step:" << sqlite3_errmsg(db) << "Code:" << rc;
        }
        sqlite3_finalize(stmt);
    } else {
        qDebug() << "[CategoryRepo] add FAILED during prepare:" << sqlite3_errmsg(db) << "Code:" << rc;
    }
    return false;
}

bool CategoryRepo::removeAllCategories(const std::string& fileId128) {
    return removeAllCategoriesBatch({fileId128});
}

bool CategoryRepo::removeAllCategoriesBatch(const std::vector<std::string>& fids) {
    int categorizedDelta = 0;
    for (const auto& fid : fids) {
        if (!getItemCategoryIds(fid).empty()) {
            s_uncategorizedCount.fetch_add(1);
            s_categorizedCount.fetch_sub(1);
            categorizedDelta--;
        }
    }
    if (categorizedDelta != 0) {
        updatePersistentStat(STAT_CATEGORIZED, categorizedDelta);
    }
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

std::vector<int> CategoryRepo::getItemCategoryIds(const std::string& fid) {
    std::vector<int> ids;
    if (fid.empty()) return ids;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return ids;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT category_id FROM category_items WHERE file_id = ? AND category_id > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ids.push_back(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return ids;
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
        s_trashCount.fetch_sub(removedCount);
        updatePersistentStat("sys_trash_count", -removedCount);
    }

    return ok;
}

bool CategoryRepo::permanentlyDelete(const std::string& fid) {
    return permanentlyDeleteBatch({fid});
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
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "UPDATE categories SET parent_id=?, name=?, color=?, preset_tags=?, sort_order=?, pinned=?, encrypted=?, encrypt_hint=?, physical_frn=?, physical_path=?, icon=? WHERE id=?";
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

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
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
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "UPDATE categories SET physical_frn = ?, physical_path = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, frn);
        sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, id);
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

    // 收集所有关联了物理路径的文件夹分类
    std::vector<std::wstring> physicalDirsToDelete;
    for (int delId : toDelete) {
        Category cat = getById(delId);
        if (cat.id > 0 && !cat.physicalPath.empty()) {
            physicalDirsToDelete.push_back(cat.physicalPath);
        }
    }

    // 4. Delete only the category rows
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

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return false;

    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFid(fileId128);

    const char* sql = "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
    double addedAt = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(memStmt, 1, categoryId);
        sqlite3_bind_text(memStmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(memStmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(memStmt, 4, addedAt);
        
        if (sqlite3_step(memStmt) == SQLITE_DONE) {
            sqlite3_finalize(memStmt);

            // 如果之前未分类，增加后变成有分类，则减去 uncategorizedCount，增加 categorizedCount 并持久化
            if (getItemCategoryIds(fileId128).size() == 1) {
                s_uncategorizedCount.fetch_sub(1);
                s_categorizedCount.fetch_add(1);
                updatePersistentStat(STAT_CATEGORIZED, 1);
            }

            // 2026-08-xx 按照 Plan-126：废除此处直接调用 registerItem。
            // 归类操作不应直接触发表入库，应由物理位移（如迁移）触发 USN 信号后再由 AutoImportManager 驱动。

            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
            return true;
        }
        sqlite3_finalize(memStmt);
    }
    return false;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) {
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return false;

    const char* sql = "DELETE FROM category_items WHERE category_id = ? AND file_id = ?";
    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(memStmt, 1, categoryId);
        sqlite3_bind_text(memStmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(memStmt) == SQLITE_DONE) {
            sqlite3_finalize(memStmt);

            // 如果移除后不再有任何分类，则增加 uncategorizedCount，减少 categorizedCount 并持久化
            if (getItemCategoryIds(fileId128).empty()) {
                s_uncategorizedCount.fetch_add(1);
                s_categorizedCount.fetch_sub(1);
                updatePersistentStat(STAT_CATEGORIZED, -1);
            }

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
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return results;

    QStringList placeholders;
    for (int i = 0; i < categoryIds.size(); ++i) placeholders << "?";
    QString sql = QString("SELECT DISTINCT file_id, path_hint FROM category_items WHERE category_id IN (%1)").arg(placeholders.join(","));

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
        for (int i = 0; i < categoryIds.size(); ++i) {
            sqlite3_bind_int(stmt, i + 1, categoryIds[i]);
        }
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

    // 1. 一次性加载所有分类关联关系
    std::unordered_map<std::string, std::vector<int>> fidToCats;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT file_id, category_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int catId = sqlite3_column_int(stmt, 1);
            if (fid) fidToCats[fid].push_back(catId);
        }
        sqlite3_finalize(stmt);
    }

    // 2. 加载分类层级结构以支持递归计数
    std::map<int, int> catParentMap;
    if (sqlite3_prepare_v2(db, "SELECT id, parent_id FROM categories WHERE id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            catParentMap[sqlite3_column_int(stmt, 0)] = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    std::map<int, std::unordered_set<std::string>> catToUniqueFids;
    // 3. 遍历内存缓存，按 FID 去重并分发到各分类桶
    // 2026-07-xx 回滚：仅计算直接关联的 FID，取消自动向上递归汇总
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        // 2026-07-xx 物理对齐：只要在关联表中且非文件夹/回收站，即计入分类总数
        if (!meta.fileId128.empty() && !meta.isFolder && !meta.isTrash) {
            auto it = fidToCats.find(meta.fileId128);
            if (it != fidToCats.end()) {
                for (int catId : it->second) {
                    catToUniqueFids[catId].insert(meta.fileId128);
                }
            }
        }
    });

    for (auto const& [id, fids] : catToUniqueFids) {
        res.push_back({id, static_cast<int>(fids.size())});
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
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return;

    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, "
                      "COALESCE((SELECT value FROM system_stats WHERE key = ?), 0) + ?)";
    
    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(memStmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(memStmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(memStmt, 3, delta);
        sqlite3_step(memStmt);
        sqlite3_finalize(memStmt);
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
    
    // 批量处理后通知 UI 刷新
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
    return ok;
}

void CategoryRepo::syncCategorizedCountForFid(const std::string& /*fid*/) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;

    // 2026-06-xx 物理对账：通过全局 DISTINCT 查询重新计算已分类总数
    // 这种做法比手动增减 delta 更稳健，能自动修正因程序崩溃导致的计数偏差
    sqlite3_stmt* stmt;
    // 物理红线：分类计数必须排除回收站 (ID = -8) 和未分类 (ID = -2)
    const char* sql = "SELECT COUNT(DISTINCT file_id) FROM category_items WHERE category_id > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            int oldCount = s_categorizedCount.load();
            s_categorizedCount.store(count);
            
            // 物理持久化：直接更新增量
            if (count != oldCount) {
                updatePersistentStat(STAT_CATEGORIZED, count - oldCount);
            }
        }
        sqlite3_finalize(stmt);
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
    // 物理加固：若元数据管理器尚未加载完成，且快照为空，拒绝重算以防止内存计数器归零并覆盖数据库
    if (!MetadataManager::instance().isLoaded()) {
        qDebug() << "[Recount] MetadataManager has not finished loading. Abort recount to prevent zeroing stats.";
        return;
    }

    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;

    // 1. 获取所有已分类的 FID
    std::unordered_set<std::string> categorizedFids;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (fid) categorizedFids.insert(fid);
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. 物理核对对账
    int total = 0;
    int tags = 0;
    int recentlyVisited = 0;
    int untagged = 0;
    int uncategorized = 0;
    int trash = 0;

    QSet<QString> uniqueTags;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
        if (meta.fileId128.empty()) continue;
        if (meta.isFolder) continue;

        if (meta.isTrash) {
            trash++;
            continue;
        }

        total++;
        if (meta.tagsEmpty) {
            untagged++;
        } else {
            for (const QString& t : meta.tags) uniqueTags.insert(t);
        }

        if (meta.atime >= now - 86400000.0) {
            recentlyVisited++;
        }

        if (categorizedFids.find(meta.fileId128) == categorizedFids.end()) {
            uncategorized++;
        }
    }

    tags = uniqueTags.size();

    // 3. 偏差增量回填：计算实际物理盘点与当前内存原子的差值 delta 进行 fetch_add
    s_totalCount.store(total);
    {
        std::lock_guard<std::mutex> tagsLock(s_tagsMutex);
        s_globalTagsSet = uniqueTags;
        s_tagsCount.store(tags);
    }
    s_recentlyVisitedCount.store(recentlyVisited);
    s_untaggedCount.store(untagged);
    s_uncategorizedCount.store(uncategorized);
    s_trashCount.store(trash);

    // 4. 将这些准确数据持久化回数据库中
    SqlTransaction trans(db);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        auto saveStat = [&](const char* key, int val) {
            sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, val);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        };
        saveStat("sys_total_count", total);
        saveStat("sys_tags_count", tags);
        saveStat("sys_recently_visited_count", recentlyVisited);
        saveStat("sys_untagged_count", untagged);
        saveStat("sys_uncategorized_count", uncategorized);
        saveStat("sys_trash_count", trash);
        sqlite3_finalize(stmt);
    }
    trans.commit();

    qDebug() << "[Recount] Backstage Recount calibration completed. Total =" << total << "Uncategorized =" << uncategorized << "Trash =" << trash;

    // 2026-06-xx 核心逻辑升级：物理有效性对账 (盘点 FRN)
    // 这一步在后台异步执行，验证文件是否被第三方删除。若失效，直接物理清退。
    // 使用 [db] 显式捕获数据库指针，并增加错误检查
    (void)QtConcurrent::run([db, snapshot]() {
        if (!db) return;

        std::vector<std::pair<std::wstring, std::string>> itemsToCheck;
        for (const auto& meta : snapshot) {
            // 只对非回收站的文件进行物理校验
            if (!meta.isFolder && !meta.isTrash) {
                itemsToCheck.push_back({meta.path, meta.fileId128});
            }
        }

        std::vector<std::wstring> pathsToRemove;
        for (const auto& item : itemsToCheck) {
            std::string currentFid;
            // 通过 WinAPI 直接检查物理文件是否存在且 ID 匹配
            bool exists = MetadataManager::fetchWinApiMetadataDirect(item.first, currentFid);
            if (!exists || currentFid != item.second) {
                // 物理校验失败：文件已被删除或移出，加入删除列表
                pathsToRemove.push_back(item.first);
            }
        }

        if (!pathsToRemove.empty()) {
            qDebug() << "[Recount] 物理校验发现" << pathsToRemove.size() << "个失效项，准备在安全线程彻底物理清退";
            QMetaObject::invokeMethod(&MetadataManager::instance(), [pathsToRemove]() {
                QStringList qPaths;
                for (const auto& p : pathsToRemove) {
                    qPaths.append(QString::fromStdWString(p));
                }
                MetadataManager::instance().removeMetadataBatchSync(qPaths);
                
                DatabaseManager::instance().flushAll();
                MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            }, Qt::BlockingQueuedConnection);
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
                else if (type == "uncategorized" && !meta.fileId128.empty() && categorizedIds.find(meta.fileId128) == categorizedIds.end()) match = true;
            }
        }
        
        if (match && !finalPath.empty()) paths << QString::fromStdWString(finalPath);
    });
    return paths;
}

struct ScanNode {
    std::wstring path;
    std::wstring name;
    uint64_t frn = 0;
    bool isDir = false;
    std::vector<ScanNode> children;
    std::vector<std::wstring> files;
};

static void scanPhysicalDirectory(const QString& currentPath, ScanNode& node) {
    QDir currentDir(currentPath);
    QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : list) {
        std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        if (fi.isDir()) {
            std::string fid;
            std::wstring frnStr;
            if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                try {
                    ScanNode childNode;
                    childNode.path = wPath;
                    childNode.name = fi.fileName().toStdWString();
                    childNode.frn = std::stoull(frnStr, nullptr, 16);
                    childNode.isDir = true;
                    scanPhysicalDirectory(fi.absoluteFilePath(), childNode);
                    node.children.push_back(std::move(childNode));
                } catch (...) {}
            }
        } else {
            node.files.push_back(wPath);
        }
    }
}

void CategoryRepo::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // ----------------------------------------------------
    // 【第一阶段】：纯 I/O 目录树收集，绝对不持任何 DB 写锁，杜绝假死
    // ----------------------------------------------------
    ScanNode rootNode;
    rootNode.path = rootPath;
    QFileInfo rootInfo(QString::fromStdWString(rootPath));
    rootNode.name = rootInfo.fileName().toStdWString();
    
    std::string rootFid;
    std::wstring rootFrnStr;
    if (!MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        return; 
    }
    try {
        rootNode.frn = std::stoull(rootFrnStr, nullptr, 16);
    } catch (...) { return; }
    rootNode.isDir = true;

    // 同步纯磁盘递归扫描，此时数据库不被上任何锁
    scanPhysicalDirectory(QString::fromStdWString(rootPath), rootNode);

    // ----------------------------------------------------
    // 【第二阶段】：超高速、高安全性纯内存与 CPU 对账，并开启极速写事务
    // ----------------------------------------------------
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    SqlTransaction trans(db);

    int rootCatId = CategoryRepo::findByFrn(rootNode.frn);
    if (rootCatId == 0) {
        std::wstring parentPath = rootInfo.absolutePath().toStdWString();
        std::string parentFid;
        std::wstring parentFrnStr;
        int parentCatId = 0;
        if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
            try {
                uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                parentCatId = CategoryRepo::findByFrn(pFrn);
            } catch (...) {}
        }

        Category cat;
        if (rootInfo.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            cat.parentId = 0;
        } else {
            cat.parentId = parentCatId;
        }
        cat.name = rootNode.name;
        cat.physicalFrn = rootNode.frn;
        cat.physicalPath = rootNode.path;
        if (CategoryRepo::add(cat)) {
            rootCatId = cat.id;
            MetadataManager::instance().registerItem(rootPath, true);
        }
    }

    if (rootCatId <= 0) {
        trans.commit();
        return;
    }

    std::vector<std::wstring> collectedFilesToProcess;

    // 递归对账 lambda 函数
    std::function<void(const ScanNode&, int)> processNode;
    processNode = [&](const ScanNode& node, int parentCatId) {
        // 1. 处理子文件夹分类对账
        for (const auto& childNode : node.children) {
            // 【核心加固】：优先使用物理 FRN 指纹绝对唯一性检索，防止同名不同物理路径的映射重叠冲突
            int existingId = CategoryRepo::findByFrn(childNode.frn);
            if (existingId == 0) {
                // 如果指纹未命中，再尝试根据父分类ID和名字在数据库查找（处理可能的历史空 FRN 数据）
                existingId = CategoryRepo::findCategoryId(parentCatId, childNode.name);
                if (existingId > 0) {
                    // 若名字匹配了，检查其原有的 FRN。如果是空白或不符，安全修复并升级为 FRN 指纹标识
                    Category existingCat = CategoryRepo::getById(existingId);
                    if (existingCat.physicalFrn == 0 || existingCat.physicalFrn != childNode.frn) {
                        CategoryRepo::updatePhysicalMapping(existingId, childNode.frn, childNode.path);
                    }
                } else {
                    // 彻底未命中任何已知记录，新建分类
                    Category cat;
                    cat.parentId = parentCatId;
                    cat.name = childNode.name;
                    cat.physicalFrn = childNode.frn;
                    cat.physicalPath = childNode.path;
                    if (CategoryRepo::add(cat)) {
                        existingId = cat.id;
                        MetadataManager::instance().registerItem(childNode.path, true);
                    }
                }
            } else {
                // 指纹命中了，但可能物理路径由于用户外部移动发生过位移，执行安全升级修复路径关联
                Category existingCat = CategoryRepo::getById(existingId);
                if (existingCat.physicalPath != childNode.path || existingCat.parentId != parentCatId) {
                    existingCat.physicalPath = childNode.path;
                    existingCat.parentId = parentCatId;
                    CategoryRepo::update(existingCat);
                }
            }

            if (existingId > 0) {
                processNode(childNode, existingId);
            }
        }

        // 2. 收集此节点下的文件供批量多媒体提取与注册使用
        for (const auto& fPath : node.files) {
            collectedFilesToProcess.push_back(fPath);
            if (parentCatId > 0) {
                std::string fid;
                if (MetadataManager::fetchWinApiMetadataDirect(fPath, fid)) {
                    CategoryRepo::addItemToCategory(parentCatId, fid, fPath);
                }
            }
        }
    };

    processNode(rootNode, rootCatId);

    trans.commit();

    // ----------------------------------------------------
    // 【第三阶段】：异步投递多媒体高级特征提取流水线，解决断流 Bug
    // ----------------------------------------------------
    if (!collectedFilesToProcess.empty()) {
        QStringList qPathsToRegister;
        for (const auto& fp : collectedFilesToProcess) {
            qPathsToRegister.append(QString::fromStdWString(fp));
        }
        // 调用 registerItemsAsync，完美一键批处理在后台将文件塞入多媒体解析提取队列 (enqueueBatch)
        MetadataManager::instance().registerItemsAsync(qPathsToRegister, true);
        qDebug() << "[AutoImport] [Pipeline_Bridge] 已将" << qPathsToRegister.size() << "个新导入文件全部推入异步多媒体高级特征提取队列";
    }
}

} // namespace ArcMeta
