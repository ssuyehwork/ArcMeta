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

std::vector<int> CategoryRepo::getItemCategoryIds(const std::string& fid) {
    std::vector<int> ids;
    if (fid.empty()) return ids;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return ids;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT category_id FROM category_items WHERE file_id = ? AND category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) ids.push_back(sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);
    }
    return ids;
}

bool CategoryRepo::moveToTrashBatch(const std::vector<std::string>& fids) {
    return executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)", -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        if (!path.empty()) MetadataManager::instance().setTrash(path, true);
        return true;
    });
}

bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& fids) {
    return executeFidBatch(fids, [](sqlite3* db, const std::string& fid) {
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE category_id = ? AND file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(delStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        if (!path.empty()) MetadataManager::instance().setTrash(path, false);
        return true;
    });
}

bool CategoryRepo::restoreFromTrash(const std::string& fid) {
    return restoreFromTrashBatch({fid});
}

bool CategoryRepo::permanentlyDeleteBatch(const std::vector<std::string>& fids) {
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
    int removedCount = 0;
    for (const auto& path : paths) { MetadataManager::instance().removeMetadataSync(path); removedCount++; }
    if (removedCount > 0) incrementTotalFileCount(-removedCount);
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
    if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE parent_id = ? AND name = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, parentId);
        sqlite3_bind_text16(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        int id = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
        sqlite3_finalize(stmt);
        return id;
    }
    return 0;
}

bool CategoryRepo::remove(int id) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;
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
    std::vector<std::string> fids;
    std::unordered_map<std::string, std::wstring> fidToPath;
    for (int catId : toDelete) {
        auto items = getItemsInCategory(catId);
        for (const auto& item : items) {
            if (fidToPath.find(item.fileId128) == fidToPath.end()) { fidToPath[item.fileId128] = item.pathHint; fids.push_back(item.fileId128); }
        }
    }
    executeFidBatch(fids, [&](sqlite3* innerDb, const std::string& fid) {
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(innerDb, "DELETE FROM category_items WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(innerDb, "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)", -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, fidToPath[fid].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        std::wstring path = MetadataManager::instance().getPathByFid(fid);
        if (path.empty()) path = fidToPath[fid];
        if (!path.empty()) MetadataManager::instance().setTrash(path, true);
        return true;
    });
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
    for (size_t i = 0; i < targets.size(); ++i) { targets[i]->sortOrder = static_cast<int>(i); update(*targets[i]); }
    return true;
}

bool CategoryRepo::reorderAll(bool ascending) {
    auto cats = getAll();
    std::sort(cats.begin(), cats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });
    for (size_t i = 0; i < cats.size(); ++i) { cats[i].sortOrder = static_cast<int>(i); update(cats[i]); }
    return true;
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& pathHint) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;
    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFid(fileId128);
    SqlTransaction trans(db);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            trans.commit();
            if (!finalPath.empty()) MetadataManager::instance().registerItem(finalPath);
            syncCategorizedCountForFid(fileId128);
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
    if (sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE category_id = ? AND file_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            trans.commit();
            syncCategorizedCountForFid(fileId128);
            return true;
        }
        sqlite3_finalize(stmt);
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
        for (int i = 0; i < categoryIds.size(); ++i) sqlite3_bind_int(stmt, i + 1, categoryIds[i]);
        while (sqlite3_step(stmt) == SQLITE_ROW) results.push_back({ sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "", sqlite3_column_text16(stmt, 1) ? reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1)) : L"" });
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
                if (std::find(ids.begin(), ids.end(), childId) == ids.end()) ids.push_back(childId);
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
    std::map<int, std::unordered_set<std::string>> catToUniqueFids;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (!meta.fileId128.empty() && !meta.isFolder && !meta.isTrash && !meta.isInvalid) {
            auto it = fidToCats.find(meta.fileId128);
            if (it != fidToCats.end()) { for (int catId : it->second) catToUniqueFids[catId].insert(meta.fileId128); }
        }
    });
    for (auto const& [id, fids] : catToUniqueFids) res.push_back({id, static_cast<int>(fids.size())});
    return res;
}

int CategoryRepo::getTotalFileCount() { return s_totalFileCount.load(); }
int CategoryRepo::getUncategorizedCount() { return 0; }
void CategoryRepo::setTotalFileCount(int count) { s_totalFileCount.store(count); }
void CategoryRepo::setCategorizedCount(int /*count*/) { }
void CategoryRepo::incrementTotalFileCount(int delta) { s_totalFileCount += delta; updatePersistentStat(STAT_TOTAL_FILES, delta); }
void CategoryRepo::incrementCategorizedCount(int /*delta*/) { }

void CategoryRepo::updatePersistentStat(const std::string& key, int delta) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return;
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, COALESCE((SELECT value FROM system_stats WHERE key = ?), 0) + ?)";
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
    for (const auto& fid : fids) { if (!action(db, fid)) { trans.rollback(); return false; } }
    bool ok = trans.commit();
    syncCategorizedCountForFid("");
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CountsOnly);
    return ok;
}

void CategoryRepo::syncCategorizedCountForFid(const std::string& /*fid*/) {
}

void CategoryRepo::fullRecount() {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT key, value FROM system_stats", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* keyPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (keyPtr && std::string(keyPtr) == STAT_TOTAL_FILES) s_totalFileCount.store(sqlite3_column_int(stmt, 1));
            }
            sqlite3_finalize(stmt);
        }
    }
    int total = 0;
    std::unordered_map<std::string, std::wstring> categorizedFidToPath;
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id, path_hint FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                if (fid) categorizedFidToPath[fid] = wpath ? wpath : L"";
            }
            sqlite3_finalize(stmt);
        }
    }
    for (const auto& [fid, path] : categorizedFidToPath) {
        if (path.empty()) continue;
        auto meta = MetadataManager::instance().getMeta(path);
        if (meta.fileId128.empty()) MetadataManager::instance().registerItem(path);
        else if (!meta.isManaged) MetadataManager::instance().setManaged(path, true, false);
    }
    std::vector<std::wstring> managedPaths;
    std::unordered_set<std::string> seenTotal;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.fileId128.empty()) return;
        if (categorizedFidToPath.count(meta.fileId128) || meta.hasUserOperations()) {
            if (!meta.isManaged) managedPaths.push_back(path);
            if (!meta.isFolder && !meta.isInvalid && !meta.isTrash) seenTotal.insert(meta.fileId128);
        }
    });
    total = (int)seenTotal.size();
    for (const auto& p : managedPaths) MetadataManager::instance().setManaged(p, true, false);
    s_totalFileCount.store(total);
    if (db) {
        SqlTransaction trans(db);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, STAT_TOTAL_FILES, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, total);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        trans.commit();
        DatabaseManager::instance().flushAll();
    }
    (void)QtConcurrent::run([db]() {
        if (!db) return;
        std::vector<std::pair<std::wstring, std::string>> itemsToCheck;
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            if (!meta.isFolder && !meta.isInvalid && !meta.isTrash) itemsToCheck.push_back({path, meta.fileId128});
        });
        int invalidatedCount = 0;
        for (const auto& item : itemsToCheck) {
            std::string currentFid;
            bool exists = MetadataManager::fetchWinApiMetadataDirect(item.first, currentFid);
            if (!exists || currentFid != item.second) { invalidatedCount++; MetadataManager::instance().setInvalid(item.first, true); }
        }
        if (invalidatedCount > 0) { DatabaseManager::instance().flushAll(); MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild); }
    });
}

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    std::vector<Category> results;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return results;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT c.id, c.parent_id, c.name, c.color, c.preset_tags, c.sort_order, c.pinned, c.encrypted, c.encrypt_hint FROM categories c JOIN (SELECT category_id, MAX(added_at) as last_added FROM category_items GROUP BY category_id) r ON c.id = r.category_id ORDER BY r.last_added DESC LIMIT ?";
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

int CategoryRepo::getUniqueItemCount() { return s_totalFileCount.load(); }
int CategoryRepo::getUncategorizedItemCount() { return 0; }

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> res;
    std::unordered_set<std::string> seenAll, seenRecent, seenUntagged, seenTrash, seenInvalid;
    QSet<QString> uniqueTags;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.fileId128.empty()) return;
        if (meta.isFolder) return;
        if (meta.isInvalid) { seenInvalid.insert(meta.fileId128); return; }
        if (meta.isTrash) { seenTrash.insert(meta.fileId128); return; }
        seenAll.insert(meta.fileId128);
        if (meta.tags.isEmpty()) seenUntagged.insert(meta.fileId128);
        else for (const QString& t : meta.tags) uniqueTags.insert(t);
        if (meta.atime >= now - 86400000.0) seenRecent.insert(meta.fileId128);
    });
    res["all"] = static_cast<int>(seenAll.size());
    res["tags"] = static_cast<int>(uniqueTags.size());
    res["recently_visited"] = static_cast<int>(seenRecent.size());
    res["untagged"] = static_cast<int>(seenUntagged.size());
    res["trash"] = static_cast<int>(seenTrash.size());
    res["invalid_data"] = static_cast<int>(seenInvalid.size());
    return res;
}

QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.isFolder) return;
        bool match = false;
        if (type == "trash") { if (meta.isTrash) match = true; }
        else if (type == "invalid_data") { if (meta.isInvalid) match = true; }
        else {
            if (meta.isInvalid) return;
            if (type == "all") { if (meta.isTrash) return; match = true; }
            else {
                if (meta.isTrash) return;
                if (type == "untagged" && meta.tags.isEmpty()) match = true;
                else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
            }
        }
        if (match) paths << QString::fromStdWString(path);
    });
    return paths;
}

} // namespace ArcMeta
