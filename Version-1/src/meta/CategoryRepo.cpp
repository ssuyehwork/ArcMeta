#include "CategoryRepo.h"
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

// 自动对齐修复被误删的 ArcMeta.Library_ 托管根分类
void syncManagedLibraries() {
    static std::atomic<bool> s_inSync{false};
    if (s_inSync.exchange(true)) return;

    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString letter = drive.absolutePath().left(1).toUpper();
        std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
        if (volSerial == L"UNKNOWN") continue;

        std::wstring managedAbsW = MetadataManager::getManagedLibraryPath(volSerial, letter);
        if (managedAbsW.empty()) continue;

        QFileInfo libInfo(QString::fromStdWString(managedAbsW));
        if (!libInfo.exists()) continue;

        std::wstring libName = libInfo.fileName().toStdWString();

        // 检查数据库中是否存在 parent_id = 0 且名称为 ArcMeta.Library_X 的分类
        int existingId = CategoryRepo::findCategoryId(0, libName);
        if (existingId == 0) {
            Category cat;
            cat.parentId = 0;
            cat.name = libName;
            cat.color = L"#378ADD";
            cat.physicalPath = managedAbsW;
            cat.icon = L"folder_filled";
            cat.kind = CategoryKind::SystemLibrary; // 显式标记为托管根分类
            
            CategoryRepo::add(cat);
            qWarning() << "[CategoryRepo] 自动修复补全托管根分类:" << QString::fromStdWString(libName);
        } else {
            CategoryRepo::updatePhysicalMapping(existingId, 0, managedAbsW);
        }
    }

    s_inSync.store(false);
}

std::vector<Category> CategoryRepo::getAll() {
    // 启动/读取时自动修复补全缺失的 ArcMeta.Library_ 根分类
    syncManagedLibraries();

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
            s_countsDirty.store(true);
            refreshMemoryCache();
            return true;
        } else {
            qCritical() << "[CategoryRepo] add FAILED during step:" << sqlite3_errmsg(mainDb) << "Code:" << rc;
        }
        sqlite3_finalize(stmt);
    } else {
        qCritical() << "[CategoryRepo] add FAILED during prepare:" << sqlite3_errmsg(mainDb) << "Code:" << rc;
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

    s_countsDirty.store(true);
    refreshMemoryCache();
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);

    return allOk;
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
            MetadataManager::instance().setTrash(path, false);
        }
        
        // 2026-06-xx 物理对账：恢复后触发全量统计重建
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        return true;
    });

    if (ok) {
        int delta = static_cast<int>(folderIds.size());
        s_uncategorizedCount.fetch_add(delta);
        s_trashCount.fetch_sub(delta);
        updatePersistentStat("sys_uncategorized_count", delta);
        updatePersistentStat("sys_trash_count", -delta);
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
        s_countsDirty.store(true);
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

    s_countsDirty.store(true);
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

            refreshMemoryCache();

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

            refreshMemoryCache();

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
        // 🚨 修复：通过 JOIN 关联只统计非文件夹 (is_folder = 0) 且非回收站的真实文件
        const char* sql = "SELECT ci.folder_id, ci.category_id FROM category_items ci "
                          "JOIN metadata m ON ci.folder_id = m.folder_id "
                          "WHERE ci.category_id > 0 AND m.is_folder = 0 AND m.is_trash = 0"; 
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
    refreshMemoryCache();
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
                          "(SELECT id FROM categories WHERE category_kind = 1)";
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

    // 修正后的未分类数量查询：仅当项目未关联任何 category_kind = 0 (用户自定义分类) 时，才计入未分类数量！
    const char* uncategorizedSql = 
        "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 0 AND folder_id NOT IN ("
        "    SELECT ci.folder_id FROM category_items ci "
        "    JOIN categories c ON ci.category_id = c.id "
        "    WHERE c.category_kind = 0"  // 物理显式筛选用户分类
        ");";
        
    sqlite3_stmt* stmtUncat = nullptr;
    if (sqlite3_prepare_v2(db, uncategorizedSql, -1, &stmtUncat, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmtUncat) == SQLITE_ROW) {
            s_uncategorizedCount.store(sqlite3_column_int(stmtUncat, 0));
        }
        sqlite3_finalize(stmtUncat);
    }
}

void CategoryRepo::fullRecount() {
    // 🚨 重新计数时，先执行 SQL 补全对账，防止根分类计数被归零！
    const char* repairSql = 
        "INSERT OR IGNORE INTO category_items (category_id, folder_id, path_hint, added_at) "
        "SELECT c.id, m.folder_id, m.path, m.added_at "
        "FROM categories c "
        "JOIN metadata m ON m.path LIKE (c.physical_path || '%') "
        "WHERE c.parent_id = 0 AND c.physical_path IS NOT NULL AND c.physical_path != '';";
    
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        if (db) {
            sqlite3_exec(db, repairSql, nullptr, nullptr, nullptr);
        }
    }

    s_countsDirty.store(true);
    getCounts(); // 物理强制重新更新
}

void CategoryRepo::fullRecountAsync(std::function<void(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts)> callback) {
    (void)QtConcurrent::run([callback]() {
        CategoryRepo::fullRecount();
        auto sysCounts = CategoryRepo::getSystemCounts(); // 1. 直接获取准确的受控资产文件总数 (如 403)
        auto catCountsVec = CategoryRepo::getCounts();    // 2. 获取各个分类目录各自的项数
        
        QMap<int, int> catCounts;
        for (const auto& entry : catCountsVec) {
            catCounts[entry.first] = entry.second;
        }

        if (callback) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [callback, sysCounts, catCounts]() {
                callback(sysCounts, catCounts);
            });
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

StatisticsSnapshot CategoryRepo::calculateAllStatistics() {
    StatisticsSnapshot snapshot;

    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

    // ----------------------------------------------------
    // 步骤 2（静态层计算）
    // ----------------------------------------------------
    
    // 1. "all"：所有库中 is_trash = 0 且 is_folder = 0 的素材总和；
    int allCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                allCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["all"] = allCount;

    // 2. "trash"：各库 is_trash = 1 的总和 + 各盘 disk_trash 表记录总和；
    int trashCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        // 托管模式回收站
        sqlite3_stmt* stmtLib = nullptr;
        const char* sqlLib = "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 1";
        if (sqlite3_prepare_v2(db, sqlLib, -1, &stmtLib, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtLib) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtLib, 0);
            }
            sqlite3_finalize(stmtLib);
        }
        // 磁盘物理回收站
        sqlite3_stmt* stmtDisk = nullptr;
        const char* sqlDisk = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sqlDisk, -1, &stmtDisk, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtDisk) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtDisk, 0);
            }
            sqlite3_finalize(stmtDisk);
        }
    }
    snapshot.systemCounts["trash"] = trashCount;

    // 3. "untagged"：所有库中 is_trash = 0 且 (tags IS NULL OR tags = '') 的素材总和；
    int untaggedCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND (tags IS NULL OR tags = '')";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                untaggedCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["untagged"] = untaggedCount;

    // 4. "uncategorized"：所有库中 is_trash = 0 且其 folder_id 从未出现在任何用户自定义分类（category_kind == 0）关联中的素材总和。
    int uncategorizedCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        // 查所有 is_trash = 0 且 is_folder = 0 的 metadata，看它们是否在用户自定义分类(category_kind=0)中有关联
        const char* sql = 
            "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 0 AND is_folder = 0 AND folder_id NOT IN ("
            "    SELECT ci.folder_id FROM category_items ci "
            "    JOIN categories c ON ci.category_id = c.id "
            "    WHERE c.category_kind = 0"
            ")";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                uncategorizedCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["uncategorized"] = uncategorizedCount;

    // 5. "recently_visited", "tags" 为动态/辅助统计，这里可赋默认值，由 getSystemCounts() 中统一收敛，或在此填入默认 0。
    // tags 可根据 getGlobalUniqueTags().size() 来决定。
    snapshot.systemCounts["tags"] = 0;
    snapshot.systemCounts["recently_visited"] = 0;

    // ----------------------------------------------------
    // 步骤 3（半静态层计算）
    // ----------------------------------------------------
    // 遍历所有 category_kind == 1（SystemLibrary）的分类：
    // 通过其 physical_path 路由到对应盘的数据库，执行 SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0，直接将该盘的有效素材总数填入 libraryCounts[cat.id]
    auto allCats = getAll();
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::SystemLibrary) {
            sqlite3* targetDb = DatabaseManager::instance().getDbForPath(cat.physicalPath);
            int libCount = 0;
            if (targetDb) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0";
                if (sqlite3_prepare_v2(targetDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        libCount = sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
            }
            snapshot.libraryCounts[cat.id] = libCount;
        }
    }

    // ----------------------------------------------------
    // 步骤 4（全动态层计算）
    // ----------------------------------------------------
    // 遍历所有 category_kind == 0（User）的分类：
    // 统计其在 category_items 中关联的有效资产总数，填入 userCategoryCounts[cat.id]。
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::User) {
            int userCount = 0;
            for (sqlite3* db : dbs) {
                if (!db) continue;
                sqlite3_stmt* stmt = nullptr;
                // 仅统计有效资产(is_trash = 0 并且 is_folder = 0)
                const char* sql = "SELECT COUNT(DISTINCT ci.folder_id) FROM category_items ci "
                                  "JOIN metadata m ON ci.folder_id = m.folder_id "
                                  "WHERE ci.category_id = ? AND m.is_trash = 0 AND m.is_folder = 0";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, cat.id);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        userCount += sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
            }
            snapshot.userCategoryCounts[cat.id] = userCount;
        }
    }

    return snapshot;
}

void CategoryRepo::calculateAllStatisticsAsync(std::function<void(const StatisticsSnapshot&)> callback) {
    (void)QtConcurrent::run([callback]() {
        StatisticsSnapshot snapshot = calculateAllStatistics();
        if (callback) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [callback, snapshot]() {
                callback(snapshot);
            });
        }
    });
}

QMap<QString, int> CategoryRepo::getGlobalUniqueTags() { 
    QMap<QString, int> tagCounts; 
 
    // 遍历所有非回收站资产的 RuntimeMeta 标签数据 
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& /*path*/, const RuntimeMeta& meta) { 
        if (meta.isTrash || meta.isFolder) return; 
 
        // meta.tags 本身已是 QStringList / QList<QString> 容器，直接迭代 
        for (const QString& tag : meta.tags) { 
            QString cleanTag = tag.trimmed(); 
            if (!cleanTag.isEmpty()) { 
                tagCounts[cleanTag]++; 
            } 
        } 
    }); 
 
    return tagCounts; 
} 

QMap<QString, int> CategoryRepo::getRawSystemCounts() {
    QMap<QString, int> res;
    res["all"] = s_totalCount.load();
    res["tags"] = s_tagsCount.load();
    res["recently_visited"] = s_recentlyVisitedCount.load();
    res["untagged"] = s_untaggedCount.load();
    res["uncategorized"] = s_uncategorizedCount.load();
    
    // 双轨隔离：汇总资源库垃圾箱计数和所有磁盘独立回收站计数
    int diskTrashCount = 0;
    int libraryTrashCount = 0;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        // 1. 统计磁盘模式回收站项目数
        sqlite3_stmt* stmt = nullptr;
        const char* sqlDisk = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sqlDisk, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                diskTrashCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }

        // 2. 统计托管模式下已标记删除的资产总数
        sqlite3_stmt* stmtLib = nullptr;
        const char* sqlLib = "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 1";
        if (sqlite3_prepare_v2(db, sqlLib, -1, &stmtLib, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtLib) == SQLITE_ROW) {
                libraryTrashCount += sqlite3_column_int(stmtLib, 0);
            }
            sqlite3_finalize(stmtLib);
        }
    }
    res["trash"] = libraryTrashCount + diskTrashCount;
    return res;
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> sysCounts = getRawSystemCounts();
    
    // 🚀 【资产素材物理全量 COUNT】：直接统计全库 metadata 表中的受控资产文件总数（如 403 项）
    int totalAssetFiles = 0;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 OR is_trash IS NULL";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                totalAssetFiles += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    // 🚀 “全部数据”必须且只能是受控文件/素材的总项数（403），严禁错算为分类目录个数！
    sysCounts["all"] = totalAssetFiles;
    return sysCounts;
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
                              "(SELECT id FROM categories WHERE category_kind = 1)";
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
        // 1. 核心修正：彻底过滤掉 .arc 物理容器目录（不渲染 DIR 壳）
        if (meta.isFolder) return;

        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与辅助元数据文件
        QString qPath = QString::fromStdWString(path);
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) || 
            qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
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
