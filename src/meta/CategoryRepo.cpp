#include "CategoryRepo.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "sqlite3.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
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

    std::wstring finalPath = MetadataManager::normalizePath(pathHint);
    if (finalPath.empty()) finalPath = MetadataManager::instance().getPathByFid(fileId128);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, categoryId);
        sqlite3_bind_text(stmt, 2, fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, finalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
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
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
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

    sqlite3_stmt* stmt;
    const char* sql = "SELECT category_id, COUNT(file_id) FROM category_items GROUP BY category_id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            res.push_back({sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1)});
        }
        sqlite3_finalize(stmt);
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
}

void CategoryRepo::incrementCategorizedCount(int delta) {
    s_categorizedCount += delta;
}

void CategoryRepo::fullRecount() {
    // 重新计算已分类项数
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(DISTINCT file_id) FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) s_categorizedCount.store(sqlite3_column_int(stmt, 0));
            sqlite3_finalize(stmt);
        }
    }
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
    res["all"] = s_totalFileCount.load();

    // 获取已分类的 FID 集合以计算未分类
    std::unordered_set<std::string> categorizedFids;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) categorizedFids.insert(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            sqlite3_finalize(stmt);
        }
    }

    int recently = 0, untagged = 0, uncategorized = 0;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isFolder) return;
        if (meta.tags.isEmpty()) untagged++;
        if (meta.atime >= now - 86400000.0) recently++;
        if (categorizedFids.find(meta.fileId128) == categorizedFids.end()) uncategorized++;
    });

    res["recently_visited"] = recently;
    res["untagged"] = untagged;
    res["uncategorized"] = uncategorized;
    res["trash"] = 0;
    return res;
}

QStringList CategoryRepo::getSystemCategoryPaths(const QString& type) {
    QStringList paths;
    std::unordered_set<std::string> categorizedIds;
    if (type == "uncategorized") {
        sqlite3* db = DatabaseManager::instance().getGlobalDb();
        if (db) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT DISTINCT file_id FROM category_items", -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) categorizedIds.insert(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
                sqlite3_finalize(stmt);
            }
        }
    }

    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.isFolder) return;
        bool match = false;
        if (type == "all") match = true;
        else if (type == "untagged" && meta.tags.isEmpty()) match = true;
        else if (type == "recently_visited" && meta.atime >= now - 86400000.0) match = true;
        else if (type == "uncategorized" && categorizedIds.find(meta.fileId128) == categorizedIds.end()) match = true;
        if (match) paths << QString::fromStdWString(path);
    });
    return paths;
}

} // namespace ArcMeta
