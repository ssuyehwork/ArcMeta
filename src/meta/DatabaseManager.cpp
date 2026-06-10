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
    std::string utf8Path = QString::fromStdWString(diskPath).toUtf8().toStdString();
    if (sqlite3_open_v2(utf8Path.c_str(), &conn.diskDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        qDebug() << "[DB] Failed to open disk DB:" << QString::fromStdString(utf8Path);
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
            original_path TEXT,
            is_invalid INTEGER DEFAULT 0
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

        -- 系统统计表
        CREATE TABLE IF NOT EXISTS system_stats (
            key TEXT PRIMARY KEY,
            value INTEGER DEFAULT 0
        );
    )";
    char* errMsg = nullptr;
    sqlite3_exec(conn.memDb, schema, nullptr, nullptr, &errMsg);
    if (errMsg) {
        qDebug() << "[DB] Schema error:" << errMsg;
        sqlite3_free(errMsg);
    } else {
        // 2026-06-xx 按照用户要求：清理任何误入 categories 表的系统保留 ID。
        // 系统分类 ID (-1, -2) 仅作为桶位标记存在于逻辑层，绝不可作为 UI 节点存储在 categories 表中。
        const char* cleanup = "DELETE FROM categories WHERE id <= 0;";
        sqlite3_exec(conn.memDb, cleanup, nullptr, nullptr, nullptr);
    }

    // 2026-06-xx 物理加固：自动迁移旧版本数据库字段
    // 检查 metadata 是否包含 is_invalid 字段
    sqlite3_stmt* checkStmt;
    bool hasInvalidColumn = false;
    if (sqlite3_prepare_v2(conn.memDb, "PRAGMA table_info(metadata)", -1, &checkStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(checkStmt) == SQLITE_ROW) {
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 1));
            if (colName && std::string(colName) == "is_invalid") {
                hasInvalidColumn = true;
                break;
            }
        }
        sqlite3_finalize(checkStmt);
    }

    if (!hasInvalidColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 is_invalid 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN is_invalid INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
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
