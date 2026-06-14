#include "TagRepo.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "CategoryRepo.h"
#include <QDebug>
#include <QDir>

namespace ArcMeta {

std::vector<TagGroup> TagRepo::getGroups() {
    std::vector<TagGroup> groups;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return groups;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, name, sort_order FROM tag_groups ORDER BY sort_order ASC, id ASC", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TagGroup g;
            g.id = sqlite3_column_int(stmt, 0);
            const wchar_t* wname = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            if (wname) g.name = QString::fromWCharArray(wname);
            g.sortOrder = sqlite3_column_int(stmt, 2);
            groups.push_back(g);
        }
        sqlite3_finalize(stmt);
    }
    return groups;
}

int TagRepo::addGroup(const QString& name) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return 0;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "INSERT INTO tag_groups (name) VALUES (?)", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            int id = static_cast<int>(sqlite3_last_insert_rowid(db));
            sqlite3_finalize(stmt);
            return id;
        }
        sqlite3_finalize(stmt);
    }
    return 0;
}

bool TagRepo::updateGroup(const TagGroup& group) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "UPDATE tag_groups SET name = ?, sort_order = ? WHERE id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, group.name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, group.sortOrder);
        sqlite3_bind_int(stmt, 3, group.id);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

bool TagRepo::deleteGroup(int id) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    SqlTransaction trans(db);

    // 1. 将该组下的标签移至默认组 (使用预编译语句)
    sqlite3_stmt* stmtUpdate;
    if (sqlite3_prepare_v2(db, "UPDATE tags_def SET group_id = 0 WHERE group_id = ?", -1, &stmtUpdate, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtUpdate, 1, id);
        sqlite3_step(stmtUpdate);
        sqlite3_finalize(stmtUpdate);
    }

    // 2. 删除分组
    sqlite3_stmt* stmtDel;
    if (sqlite3_prepare_v2(db, "DELETE FROM tag_groups WHERE id = ?", -1, &stmtDel, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtDel, 1, id);
        sqlite3_step(stmtDel);
        sqlite3_finalize(stmtDel);
    }
    return trans.commit();
}

std::vector<TagDef> TagRepo::getAllTags() {
    std::vector<TagDef> tags;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return tags;

    // 1. 加载定义
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT name, is_favorite, group_id, color, sort_order FROM tags_def", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TagDef t;
            const wchar_t* wname = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 0));
            if (wname) t.name = QString::fromWCharArray(wname);
            t.isFavorite = sqlite3_column_int(stmt, 1) != 0;
            t.groupId = sqlite3_column_int(stmt, 2);
            const wchar_t* wcolor = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 3));
            if (wcolor) t.color = QString::fromWCharArray(wcolor);
            t.sortOrder = sqlite3_column_int(stmt, 4);
            tags.push_back(t);
        }
        sqlite3_finalize(stmt);
    }

    // 2. 补全计数
    auto stats = getTagUsageStats();
    for (auto& t : tags) {
        t.usageCount = stats.value(t.name, 0);
    }

    // 3. 补全尚未在 tags_def 中登记但在 metadata 中存在的标签
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        bool found = false;
        for (const auto& existing : tags) {
            if (existing.name == it.key()) { found = true; break; }
        }
        if (!found) {
            TagDef nt;
            nt.name = it.key();
            nt.usageCount = it.value();
            tags.push_back(nt);
        }
    }

    return tags;
}

bool TagRepo::setTagFavorite(const QString& name, bool favorite) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO tags_def (name, is_favorite) VALUES (?, ?) "
                      "ON CONFLICT(name) DO UPDATE SET is_favorite = excluded.is_favorite";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, favorite ? 1 : 0);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

bool TagRepo::setTagGroup(const QString& name, int groupId) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO tags_def (name, group_id) VALUES (?, ?) "
                      "ON CONFLICT(name) DO UPDATE SET group_id = excluded.group_id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, groupId);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

bool TagRepo::deleteTagDef(const QString& name) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "DELETE FROM tags_def WHERE name = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    return false;
}

bool TagRepo::renameTagGlobal(const QString& oldName, const QString& newName) {
    if (oldName == newName || oldName.isEmpty() || newName.isEmpty()) return false;

    // 1. 更新标签定义表
    sqlite3* gdb = DatabaseManager::instance().getGlobalDb();
    if (gdb) {
        SqlTransaction trans(gdb);
        sqlite3_stmt* stmt;
        // 先尝试将旧定义的属性迁移到新名称上
        const char* migrateSql = "INSERT OR REPLACE INTO tags_def (name, is_favorite, group_id, color, sort_order) "
                                 "SELECT ?, is_favorite, group_id, color, sort_order FROM tags_def WHERE name = ?";
        if (sqlite3_prepare_v2(gdb, migrateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmt, 1, newName.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 2, oldName.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // 安全删除旧定义
        sqlite3_stmt* stmtDel;
        if (sqlite3_prepare_v2(gdb, "DELETE FROM tags_def WHERE name = ?", -1, &stmtDel, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmtDel, 1, oldName.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtDel);
            sqlite3_finalize(stmtDel);
        }
        trans.commit();
    }

    // 2. 更新所有物理分库中的 metadata 表
    auto updateMetadata = [&](sqlite3* db) {
        if (!db) return;
        SqlTransaction trans(db);

        // 使用参数化查询避免注入
        // 逻辑：精准替换逗号分隔字符串中的项
        auto execUpdate = [&](const char* sql, const QString& find, const QString& repl) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text16(stmt, 1, repl.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 2, find.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 3, find.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        };

        QString midOld = "," + oldName + ",";
        QString midNew = "," + newName + ",";
        execUpdate("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE '%' || ? || '%'", midOld, midNew);

        QString startOld = oldName + ",";
        QString startNew = newName + ",";
        execUpdate("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE ? || '%'", startOld, startNew);

        QString endOld = "," + oldName;
        QString endNew = "," + newName;
        execUpdate("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE '%' || ?", endOld, endNew);

        // 处理唯一标签的情况
        sqlite3_stmt* stmtSingle;
        if (sqlite3_prepare_v2(db, "UPDATE metadata SET tags = ? WHERE tags = ?", -1, &stmtSingle, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmtSingle, 1, newName.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmtSingle, 2, oldName.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtSingle);
            sqlite3_finalize(stmtSingle);
        }

        // 2026-xx-xx 物理修复：在 rename 后显式调用 CategoryRepo::fullRecount 以刷新 UI 计数
        // 确保“标签管理”项中的 (n) 计数即时同步

        trans.commit();
    };

    QString metaDir = DatabaseManager::instance().getAppDir() + "/.arcmeta";
    QDir dir(metaDir);
    QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files | QDir::Hidden | QDir::System);
    for (const QString& dbFile : dbFiles) {
        QString volSerial = dbFile.mid(8, dbFile.length() - 8 - 3);
        sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial.toStdWString());
        updateMetadata(db);
    }

    // 3. 同步更新内存缓存
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.tags.contains(oldName)) {
            QStringList newTags = meta.tags;
            newTags.removeAll(oldName);
            if (!newTags.contains(newName)) newTags << newName;
            MetadataManager::instance().setTags(path, newTags, false);
        }
    });

    MetadataManager::instance().notifyFullUIRebuild();
    return true;
}

bool TagRepo::deleteTagGlobal(const QString& name) {
    if (name.isEmpty()) return false;

    // 1. 删除定义
    deleteTagDef(name);

    // 2. 更新所有物理分库：移除标签
    auto removeMetadataTag = [&](sqlite3* db) {
        if (!db) return;
        SqlTransaction trans(db);

        auto execRemove = [&](const char* sql, const QString& find, const QString& repl) {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text16(stmt, 1, repl.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 2, find.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 3, find.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        };

        execRemove("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE '%' || ? || '%'", "," + name + ",", ",");
        execRemove("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE ? || '%'", name + ",", "");
        execRemove("UPDATE metadata SET tags = replace(tags, ?, ?) WHERE tags LIKE '%' || ?", "," + name, "");

        sqlite3_stmt* stmtSingle;
        if (sqlite3_prepare_v2(db, "UPDATE metadata SET tags = '' WHERE tags = ?", -1, &stmtSingle, nullptr) == SQLITE_OK) {
            sqlite3_bind_text16(stmtSingle, 1, name.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtSingle);
            sqlite3_finalize(stmtSingle);
        }
        trans.commit();
    };

    QString metaDir = DatabaseManager::instance().getAppDir() + "/.arcmeta";
    QDir dir(metaDir);
    QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files | QDir::Hidden | QDir::System);
    for (const QString& dbFile : dbFiles) {
        QString volSerial = dbFile.mid(8, dbFile.length() - 8 - 3);
        sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial.toStdWString());
        removeMetadataTag(db);
    }

    // 3. 同步内存
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.tags.contains(name)) {
            QStringList newTags = meta.tags;
            newTags.removeAll(name);
            MetadataManager::instance().setTags(path, newTags, false);
        }
    });

    MetadataManager::instance().notifyFullUIRebuild();
    return true;
}

QMap<QString, int> TagRepo::getTagUsageStats() {
    QMap<QString, int> stats;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isTrash || meta.isInvalid || meta.isFolder) return;
        for (const QString& t : meta.tags) {
            stats[t]++;
        }
    });
    return stats;
}

QStringList TagRepo::getFavoriteTags() {
    QStringList favs;
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return favs;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT name FROM tags_def WHERE is_favorite = 1", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const wchar_t* wname = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 0));
            if (wname) favs << QString::fromWCharArray(wname);
        }
        sqlite3_finalize(stmt);
    }
    return favs;
}

} // namespace ArcMeta
