#include "DatabaseManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <windows.h>

namespace ArcMeta {

// --- SqlTransaction 实现 ---
SqlTransaction::SqlTransaction(sqlite3* db) : m_db(db) {
    if (m_db) sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
}

SqlTransaction::~SqlTransaction() {
    if (m_db && !m_committed) sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
}

bool SqlTransaction::commit() {
    if (m_db && !m_committed) {
        if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) {
            m_committed = true;
            return true;
        }
    }
    return false;
}

void SqlTransaction::rollback() {
    if (m_db && !m_committed) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        m_committed = true; // 标记已处理
    }
}

// --- DatabaseManager 实现 ---
DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
}

DatabaseManager::~DatabaseManager() {
    closeDb(m_globalDb);
    for (auto& pair : m_driveDbs) closeDb(pair.second);
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> lock(m_mutex);
    QString metaDir = getAppDir() + "/.arcmeta";
    QDir().mkpath(metaDir);

    QString globalPath = metaDir + "/Arcmeta_Global.db";
    if (!loadDb(globalPath.toStdWString(), m_globalDb)) return false;

    auto createTables = [](sqlite3* db) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS metadata ("
            "file_id TEXT PRIMARY KEY, path TEXT, is_folder INTEGER, rating INTEGER, "
            "color TEXT, tags TEXT, note TEXT, url TEXT, ctime INTEGER, mtime INTEGER, "
            "atime INTEGER, file_size INTEGER, palettes BLOB, is_trash INTEGER, "
            "original_path TEXT, is_invalid INTEGER);"
            "CREATE TABLE IF NOT EXISTS categories ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER, name TEXT, color TEXT, "
            "preset_tags TEXT, sort_order INTEGER, pinned INTEGER, encrypted INTEGER, encrypt_hint TEXT);"
            "CREATE TABLE IF NOT EXISTS category_items ("
            "category_id INTEGER, file_id TEXT, path_hint TEXT, added_at REAL, "
            "PRIMARY KEY(category_id, file_id));"
            "CREATE TABLE IF NOT EXISTS system_stats ("
            "key TEXT PRIMARY KEY, value INTEGER);";
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);

        // 物理补完：检查是否缺失 is_invalid 字段（版本迁移）
        sqlite3_exec(db, "ALTER TABLE metadata ADD COLUMN is_invalid INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    };

    createTables(m_globalDb.memDb);
    return true;
}

QString DatabaseManager::getAppDir() {
    return QCoreApplication::applicationDirPath();
}

bool DatabaseManager::loadDb(const std::wstring& diskPath, DbConnection& conn) {
    if (sqlite3_open16(diskPath.c_str(), &conn.diskDb) != SQLITE_OK) return false;
    if (sqlite3_open(":memory:", &conn.memDb) != SQLITE_OK) return false;

    sqlite3_backup* backup = sqlite3_backup_init(conn.memDb, "main", conn.diskDb, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    }
    conn.diskPath = diskPath;
    ensureHidden(diskPath);
    return true;
}

void DatabaseManager::saveDb(DbConnection& conn) {
    if (!conn.memDb || !conn.diskDb) return;

    // 2026-06-xx 物理重构：分段步进式持久化，解决大规模数据下的 UI 假死与退出残留
    sqlite3_backup* backup = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
    if (backup) {
        int rc;
        do {
            rc = sqlite3_backup_step(backup, 100);
            QCoreApplication::processEvents();
            QThread::msleep(1);
        } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
        sqlite3_backup_finish(backup);
    }
}

void DatabaseManager::closeDb(DbConnection& conn) {
    if (conn.memDb) { sqlite3_close_v2(conn.memDb); conn.memDb = nullptr; }
    if (conn.diskDb) { sqlite3_close_v2(conn.diskDb); conn.diskDb = nullptr; }
}

void DatabaseManager::flushAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveDb(m_globalDb);
    for (auto& pair : m_driveDbs) saveDb(pair.second);
}

sqlite3* DatabaseManager::getMemoryDb(const std::wstring& volumeSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_driveDbs.find(volumeSerial) == m_driveDbs.end()) {
        QString dbPath = getAppDir() + "/.arcmeta/Arcmeta_" + QString::fromStdWString(volumeSerial) + ".db";
        DbConnection conn;
        if (loadDb(dbPath.toStdWString(), conn)) {
            m_driveDbs[volumeSerial] = conn;
        } else return nullptr;
    }
    return m_driveDbs[volumeSerial].memDb;
}

sqlite3* DatabaseManager::getGlobalDb() {
    return m_globalDb.memDb;
}

void DatabaseManager::ensureHidden(const std::wstring& path) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
}

} // namespace ArcMeta
