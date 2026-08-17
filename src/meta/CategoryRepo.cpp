#include "CategoryRepo.h"
#include "StatisticsService.h"
#include "../core/VolumeOnlineManager.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "sqlite3.h"
#include "../core/AppConfig.h"
#include "../core/CategoryLockManager.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QCoreApplication>
#include <set>
#include <unordered_set>
#include <algorithm>

namespace ArcMeta {


// 初始化静态内存快照指针
std::shared_ptr<const std::vector<Category>> CategoryRepo::s_categoryCache = std::make_shared<const std::vector<Category>>();
std::shared_ptr<const std::vector<Category>> CategoryRepo::s_recentlyUsedCache = std::make_shared<const std::vector<Category>>();
std::shared_ptr<const std::unordered_map<std::string, std::vector<int>>> CategoryRepo::s_itemCategoriesCache = std::make_shared<const std::unordered_map<std::string, std::vector<int>>>();
std::mutex CategoryRepo::s_cacheMutex;

void CategoryRepo::initialize() {
    // SQLite 模式下，DatabaseManager::init() 已由 MetadataManager 调用
    refreshMemoryCache();
}

void CategoryRepo::refreshMemoryCache() {
    // 磁盘 DB -> 内存 DB 读出，构建干净的数组快照
    auto dbCats = getAll();
    auto dbRecent = getRecentlyUsed(50); // 最多缓存 50 个最近使用的分类

    // 2. Rebuild item to categories map cache
    std::unordered_map<std::string, std::vector<int>> itemCats;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    const char* sql = "SELECT folder_id, category_id FROM category_items WHERE category_id > 0";
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                int catId = sqlite3_column_int(stmt, 1);
                if (fid) {
                    itemCats[fid].push_back(catId);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    std::lock_guard<std::mutex> lock(s_cacheMutex);
    std::atomic_store(&s_categoryCache, 
        std::shared_ptr<const std::vector<Category>>(std::make_shared<const std::vector<Category>>(std::move(dbCats))));
    std::atomic_store(&s_recentlyUsedCache, 
        std::shared_ptr<const std::vector<Category>>(std::make_shared<const std::vector<Category>>(std::move(dbRecent))));
    std::atomic_store(&s_itemCategoriesCache,
        std::shared_ptr<const std::unordered_map<std::string, std::vector<int>>>(std::make_shared<const std::unordered_map<std::string, std::vector<int>>>(std::move(itemCats))));
}

std::vector<Category> CategoryRepo::getCachedAll() {
    auto snapshot = std::atomic_load(&s_categoryCache);
    if (!snapshot) return {};
    return *snapshot; // 纯内存指针浅拷贝返回，零 SQL 耗时
}

Category CategoryRepo::getCachedById(int id) {
    auto snapshot = std::atomic_load(&s_categoryCache);
    if (!snapshot) return Category();
    for (const auto& c : *snapshot) {
        if (c.id == id) return c;
    }
    return Category();
}

std::vector<Category> CategoryRepo::getCachedRecentlyUsed(size_t limit) {
    auto snapshot = std::atomic_load(&s_recentlyUsedCache);
    if (!snapshot) return {};
    std::vector<Category> res = *snapshot;
    if (res.size() > limit) res.resize(limit);
    return res;
}

void CategoryRepo::saveImmediately() {
    DatabaseManager::instance().flushAll();
}

std::vector<Category> CategoryRepo::getAll() {
    std::vector<Category> results;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon, category_kind FROM categories WHERE id > 0 ORDER BY sort_order ASC";
    std::set<int> seenIds;

    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                if (seenIds.count(id)) continue;
                seenIds.insert(id);

                Category c;
                c.id = id;
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
                c.kind = static_cast<CategoryKind>(sqlite3_column_int(stmt, 12));
                results.push_back(c);
            }
            sqlite3_finalize(stmt);
        }
    }
    return results;
}

void CategoryRepo::bindToLibraryRootCategory(const std::string& folderId, const std::wstring& physicalPath) {
    if (folderId.empty() || physicalPath.empty()) return;

    // 1. 获取所有根分类 (parentId == 0)
    auto allCats = getAll();
    for (const auto& cat : allCats) {
        if (cat.parentId == 0 && !cat.physicalPath.empty()) {
            // 2. 检查物理路径是否属于该托管库目录 (前缀匹配)
            std::wstring normCatPath = MetadataManager::normalizePath(cat.physicalPath);
            std::wstring normPhysPath = MetadataManager::normalizePath(physicalPath);
            if (normPhysPath.rfind(normCatPath, 0) == 0) { 
                // 3. 自动向 category_items 写入绑定映射
                addItemToCategory(cat.id, folderId, physicalPath);
                break;
            }
        }
    }
}

bool CategoryRepo::add(Category& cat) {
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    if (dbs.empty()) return false;

    sqlite3* mainDb = DatabaseManager::instance().getGlobalDb();
    if (!mainDb) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon, category_kind) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
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
        sqlite3_bind_int(stmt, 12, static_cast<int>(cat.kind));

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            cat.id = static_cast<int>(sqlite3_last_insert_rowid(mainDb));
            sqlite3_finalize(stmt);

            // Now write to all OTHER active databases with the same explicit ID!
            const char* sqlWithId = "INSERT OR REPLACE INTO categories (id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon, category_kind) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
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
                    sqlite3_bind_int(stmtOther, 13, static_cast<int>(cat.kind));
                    sqlite3_step(stmtOther);
                    sqlite3_finalize(stmtOther);
                }
            }
            refreshMemoryCache();
            return true;
        } else {
        }
        sqlite3_finalize(stmt);
    } else {
    }
    return false;
}

bool CategoryRepo::addItemToCategoryBatch(int categoryId, const std::vector<std::pair<std::string, std::wstring>>& items) {
    if (items.empty()) return true;

    WriteGuard guard;

    // Group items by database connection
    std::map<sqlite3*, std::vector<std::pair<std::string, std::wstring>>> dbToItems;
    for (const auto& item : items) {
        std::wstring finalPath = MetadataManager::normalizePath(item.second);
        if (finalPath.empty()) {
            finalPath = MetadataManager::instance().getPathByFolderId(item.first);
        }
        sqlite3* db = DatabaseManager::instance().getDbForPath(finalPath);
        if (db) {
            dbToItems[db].push_back({item.first, finalPath});
        }
    }

    bool allOk = true;
    double addedAt = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    for (auto& [db, itemsInDb] : dbToItems) {
        SqlTransaction trans(db);
        bool transOk = true;

        const char* sql = "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            for (const auto& pair : itemsInDb) {
                sqlite3_reset(stmt);
                sqlite3_bind_int(stmt, 1, categoryId);
                sqlite3_bind_text(stmt, 2, pair.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 3, pair.second.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 4, addedAt);

                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    transOk = false;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        } else {
            transOk = false;
        }

        if (transOk) {
            trans.commit();
        } else {
            trans.rollback();
            allOk = false;
        }
    }

    if (allOk) {
        for (const auto& pair : items) {
            std::wstring p = MetadataManager::normalizePath(pair.second);
            if (p.empty()) p = MetadataManager::instance().getPathByFolderId(pair.first);
            if (!p.empty()) {
                MetadataManager::instance().addCategoryToItemMemory(p, categoryId);
                MetadataManager::instance().persistAsync(p, false);
            }
        }
    }

    refreshMemoryCache();
    StatisticsService::instance().requestFullRecountAsync();
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);

    return allOk;
}

bool CategoryRepo::removeAllCategories(const std::string& folderId) {
    return removeAllCategoriesBatch({folderId});
}

bool CategoryRepo::removeAllCategoriesBatch(const std::vector<std::string>& folderIds) {
    for (const auto& fid : folderIds) {
        std::wstring p = MetadataManager::instance().getPathByFolderId(fid);
        if (!p.empty()) {
            MetadataManager::instance().clearCategoriesFromItemMemory(p);
        }
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

std::vector<int> CategoryRepo::getItemCategoryIds(const std::string& folderId, const std::wstring& /*pathHint*/) {
    if (folderId.empty()) return {};
    auto snapshot = std::atomic_load(&s_itemCategoriesCache);
    if (!snapshot) return {};
    auto it = snapshot->find(folderId);
    if (it != snapshot->end()) {
        return it->second;
    }
    return {};
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
        // 后备查询：若内存路径为空，直接执行 SELECT path FROM metadata WHERE folder_id = ? 获取真实路径
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        if (path.empty()) {
            sqlite3_stmt* selStmt = nullptr;
            const char* sqlSel = "SELECT path FROM metadata WHERE folder_id = ?";
            if (sqlite3_prepare_v2(db, sqlSel, -1, &selStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(selStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(selStmt) == SQLITE_ROW) {
                    const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(selStmt, 0));
                    if (wpath) path = wpath;
                }
                sqlite3_finalize(selStmt);
            }
        }
        // 2. Insert into trash bucket
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
            MetadataManager::instance().clearCategoriesFromItemMemory(path);
            MetadataManager::instance().setTrash(path, true);
        }
        // 执行 UPDATE metadata SET is_trash = 1 WHERE folder_id = ? 将标记置为 1
        sqlite3_stmt* updStmt = nullptr;
        const char* sqlUpd = "UPDATE metadata SET is_trash = 1 WHERE folder_id = ?";
        if (sqlite3_prepare_v2(db, sqlUpd, -1, &updStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(updStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(updStmt);
            sqlite3_finalize(updStmt);
        }
        return true;
    });

    refreshMemoryCache();
    StatisticsService::instance().requestFullRecountAsync();
    return true;
}

bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& folderIds) {
    if (folderIds.empty()) return true;

    bool ok = executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
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
            MetadataManager::instance().clearCategoriesFromItemMemory(path);
            MetadataManager::instance().setTrash(path, false);
        }
        
        // 2026-06-xx 物理对账：恢复后触发全量统计重建
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        return true;
    });

    if (ok) {
        refreshMemoryCache();
        StatisticsService::instance().requestFullRecountAsync();
    }

    return ok;
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


    return ok;
}

bool CategoryRepo::permanentlyDelete(const std::string& folderId) {
    return permanentlyDeleteBatch({folderId});
}

int CategoryRepo::getLibraryCategoryIdByDrive(const QString& driveLetter) {
    if (driveLetter.isEmpty()) return 0;
    auto cats = getCachedAll();
    for (const auto& cat : cats) {
        if (cat.kind == CategoryKind::SystemLibrary) {
            QString nameStr = QString::fromStdWString(cat.name).toLower();
            QString letter = VolumeOnlineManager::extractDriveLetter(nameStr);
            if (letter.isEmpty() && !cat.physicalPath.empty()) {
                letter = VolumeOnlineManager::extractDriveLetter(QString::fromStdWString(cat.physicalPath));
            }
            if (!letter.isEmpty() && letter.compare(driveLetter, Qt::CaseInsensitive) == 0) {
                return cat.id;
            }
        }
    }
    return 0;
}

Category CategoryRepo::getById(int id) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    Category c;
    if (!db) return c;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint, physical_frn, physical_path, icon, category_kind FROM categories WHERE id = ?";
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
            c.kind = static_cast<CategoryKind>(sqlite3_column_int(stmt, 12));
        }
        sqlite3_finalize(stmt);
    }
    return c;
}

bool CategoryRepo::update(const Category& cat) {
    WriteGuard guard;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    const char* sql = "UPDATE categories SET parent_id=?, name=?, color=?, preset_tags=?, sort_order=?, pinned=?, encrypted=?, encrypt_hint=?, physical_frn=?, physical_path=?, icon=?, category_kind=? WHERE id=?";
    
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
            sqlite3_bind_int(stmt, 12, static_cast<int>(cat.kind));
            sqlite3_bind_int(stmt, 13, cat.id);

            if (sqlite3_step(stmt) == SQLITE_DONE) anyOk = true;
            sqlite3_finalize(stmt);
        }
    }
    if (anyOk) {
        refreshMemoryCache();
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
    if (anyOk) {
        refreshMemoryCache();
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

    // ✅ 修正后：删除分类只清理数据库与内存关联，严禁物理删除用户磁盘上的实际文件夹！
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
    refreshMemoryCache();
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
    refreshMemoryCache();
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
    refreshMemoryCache();
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
            DatabaseManager::instance().flushAll();
            refreshMemoryCache();
            return true;
        }
    }
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
        refreshMemoryCache();
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

            MetadataManager::instance().addCategoryToItemMemory(finalPath, categoryId);

            refreshMemoryCache();
            StatisticsService::instance().requestFullRecountAsync();

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

            MetadataManager::instance().removeCategoryFromItemMemory(path, categoryId);

            refreshMemoryCache();
            StatisticsService::instance().requestFullRecountAsync();

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

    refreshMemoryCache();
    // 批量处理后通知 UI 刷新
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
    return allOk;
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


QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    std::unordered_set<std::string> categorizedIds;
    if (type == "uncategorized") {
        // 1. 提取所有属于 ③ 的用户自定义分类 ID
        std::unordered_set<int> userCatIds;
        auto allCats = CategoryRepo::getCachedAll();
        for (const auto& cat : allCats) {
            if (cat.kind == CategoryKind::User && cat.id > 0) {
                userCatIds.insert(cat.id);
            }
        }

        // 2. 收集所有已归入 ③ 的 folder_id
        if (!userCatIds.empty()) {
            auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
            QStringList placeholders;
            for (size_t i = 0; i < userCatIds.size(); ++i) placeholders << "?";
            QString sql = QString("SELECT DISTINCT folder_id FROM category_items WHERE category_id IN (%1)").arg(placeholders.join(","));
            QByteArray sqlUtf8 = sql.toUtf8();

            for (sqlite3* db : dbs) {
                if (!db) continue;
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sqlUtf8.constData(), -1, &stmt, nullptr) == SQLITE_OK) {
                    int bindIdx = 1;
                    for (int cid : userCatIds) {
                        sqlite3_bind_int(stmt, bindIdx++, cid);
                    }
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        if (fid) categorizedIds.insert(fid);
                    }
                    sqlite3_finalize(stmt);
                }
            }
        }
    }

    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        // 1. 核心修正：彻底过滤掉 .arc 物理容器目录（不渲染 DIR 壳）
        if (meta.isFolder) return;

        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与辅助元数据文件
        QString qPath = QString::fromStdWString(path);
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
            return;
        }

        // 🚨 3. 安全防护：若该资产被划分到了加锁分类中且尚未解锁，则在聚合视图中物理隐藏之！
        if (!meta.folderId.empty()) {
            std::vector<int> associatedCatIds = CategoryRepo::getItemCategoryIds(meta.folderId, path);
            for (int cid : associatedCatIds) {
                if (cid > 0) {
                    Category assocCat = CategoryRepo::getCachedById(cid);
                    if (assocCat.encrypted && !CategoryLockManager::instance().isUnlocked(cid)) {
                        return; // 物理强行跳过，杜绝外溢泄露！
                    }
                }
            }
        }

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
                else if (type == "recently_visited") {
                    long long activeTime = meta.atime > 0 ? meta.atime : std::max({meta.mtime, meta.ctime, meta.added_at});
                    if (static_cast<double>(activeTime) >= now - 86400000.0) match = true;
                }
                else if (type == "uncategorized" && !meta.folderId.empty() && categorizedIds.find(meta.folderId) == categorizedIds.end()) match = true;
            }
        }
        
        if (match && !finalPath.empty()) paths << QString::fromStdWString(finalPath);
    });
    return paths;
}

} // namespace ArcMeta
