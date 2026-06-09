#include "DatabaseManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <windows.h>
#include "MetadataManager.h"

namespace ArcMeta {

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
}

DatabaseManager::~DatabaseManager() {
    flushAll();
    for (auto& pair : m_driveDbs) {
        closeDb(pair.second);
    }
    closeDb(m_globalDb);
}

QString DatabaseManager::getAppDir() {
    return QCoreApplication::applicationDirPath();
}

void DatabaseManager::ensureHidden(const std::wstring& path) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
}

bool DatabaseManager::loadDb(const std::wstring& diskPath, DbConnection& conn) {
    if (sqlite3_open_v2(reinterpret_cast<const char*>(QString::fromStdWString(diskPath).toUtf8().constData()), &conn.diskDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        return false;
    }
    ensureHidden(diskPath);

    if (sqlite3_open(":memory:", &conn.memDb) != SQLITE_OK) {
        sqlite3_close(conn.diskDb);
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(conn.memDb, "main", conn.diskDb, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    }
    // 初始化表结构 (Schema)
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS metadata (
            file_id TEXT PRIMARY KEY,
            path TEXT NOT NULL,
            is_folder INTEGER DEFAULT 0,
            rating INTEGER DEFAULT 0,
            color TEXT,
            tags TEXT,
            note TEXT,
            url TEXT,
            ctime INTEGER,
            mtime INTEGER,
            atime INTEGER,
            file_size INTEGER,
            palettes BLOB,
            is_trash INTEGER DEFAULT 0,
            original_path TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);

        -- 分类定义表
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER DEFAULT 0,
            name TEXT NOT NULL,
            color TEXT,
            preset_tags TEXT,
            sort_order INTEGER DEFAULT 0,
            pinned INTEGER DEFAULT 0,
            encrypted INTEGER DEFAULT 0,
            encrypt_hint TEXT
        );

        -- 分类与项目关联表
        CREATE TABLE IF NOT EXISTS category_items (
            category_id INTEGER,
            file_id TEXT,
            path_hint TEXT,
            added_at REAL,
            PRIMARY KEY (category_id, file_id)
        );
    )";
    char* errMsg = nullptr;
    sqlite3_exec(conn.memDb, schema, nullptr, nullptr, &errMsg);
    if (errMsg) {
        qDebug() << "[DB] Schema error:" << errMsg;
        sqlite3_free(errMsg);
    }

    conn.diskPath = diskPath;
    return true;
}

void DatabaseManager::saveDb(DbConnection& conn) {
    if (!conn.memDb || !conn.diskDb) return;
    sqlite3_backup* backup = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    }
}

void DatabaseManager::closeDb(DbConnection& conn) {
    saveDb(conn);
    if (conn.memDb) sqlite3_close(conn.memDb);
    if (conn.diskDb) sqlite3_close(conn.diskDb);
    conn.memDb = nullptr;
    conn.diskDb = nullptr;
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> lock(m_mutex);
    QString metaDir = getAppDir() + "/.arcmeta";
    QDir().mkpath(metaDir);
    ensureHidden(metaDir.toStdWString());

    // 加载全局库
    std::wstring globalPath = (metaDir + "/global.db").toStdWString();
    loadDb(globalPath, m_globalDb);

    // 为每个驱动器加载数据库
    // 注意：此处实际应遍历当前在线的驱动器，这里先简化逻辑
    // 实际运行时，MetadataManager 会按需通过 getMemoryDb 触发加载或由 init 调用
    return true;
}

void DatabaseManager::flushAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveDb(m_globalDb);
    for (auto& pair : m_driveDbs) {
        saveDb(pair.second);
    }
}

sqlite3* DatabaseManager::getMemoryDb(const std::wstring& volumeSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_driveDbs.find(volumeSerial) == m_driveDbs.end()) {
        QString dbPath = getAppDir() + "/.arcmeta/Arcmeta_" + QString::fromStdWString(volumeSerial) + ".db";
        DbConnection conn;
        if (loadDb(dbPath.toStdWString(), conn)) {
            m_driveDbs[volumeSerial] = conn;
        } else {
            return nullptr;
        }
    }
    return m_driveDbs[volumeSerial].memDb;
}

sqlite3* DatabaseManager::getGlobalDb() {
    return m_globalDb.memDb;
}

} // namespace ArcMeta
