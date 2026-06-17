#include "DatabaseManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <windows.h>
#include "MetadataManager.h"

namespace ArcMeta {

SqlTransaction::SqlTransaction(struct sqlite3* db) : m_db(db) {
    if (m_db) {
        // 2026-07-xx 物理修复 (1.22)：通过检测 autocommit 状态支持伪嵌套事务。
        // 如果 autocommit 为 0，说明已经处于外部事务中。
        m_isNested = (sqlite3_get_autocommit(m_db) == 0);
        
        if (!m_isNested) {
            // 2026-06-xx 物理加固：内置针对 SQLITE_BUSY 的重试机制
            int retry = 0;
            while (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) == SQLITE_BUSY && retry++ < 5) {
                Sleep(50);
            }
        }
    }
}

SqlTransaction::~SqlTransaction() {
    if (m_db && !m_committed && !m_isNested) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

bool SqlTransaction::commit() {
    if (m_db && !m_committed) {
        if (m_isNested) {
            m_committed = true;
            return true;
        }
        if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) {
            m_committed = true;
            return true;
        }
    }
    return false;
}

void SqlTransaction::rollback() {
    if (m_db && !m_committed) {
        if (!m_isNested) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        m_committed = true; // Mark as "processed" to prevent dtor rollback
    }
}

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
            is_invalid INTEGER DEFAULT 0,
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0
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

        -- 标签组表
        CREATE TABLE IF NOT EXISTS tag_groups (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            color TEXT,
            sort_order INTEGER DEFAULT 0
        );

        -- 标签与标签组关联表
        CREATE TABLE IF NOT EXISTS tag_group_items (
            group_id INTEGER,
            tag_name TEXT,
            PRIMARY KEY (group_id, tag_name)
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

    // 2026-07-xx 物理加固：自动迁移旧版本数据库字段 (Plan-29)
    sqlite3_stmt* checkStmt;
    bool hasInvalidColumn = false;
    bool hasWidthColumn = false;
    bool hasHeightColumn = false;

    if (sqlite3_prepare_v2(conn.memDb, "PRAGMA table_info(metadata)", -1, &checkStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(checkStmt) == SQLITE_ROW) {
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 1));
            if (colName) {
                std::string name(colName);
                if (name == "is_invalid") hasInvalidColumn = true;
                if (name == "width") hasWidthColumn = true;
                if (name == "height") hasHeightColumn = true;
            }
        }
        sqlite3_finalize(checkStmt);
    }

    if (!hasInvalidColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 is_invalid 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN is_invalid INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    }
    if (!hasWidthColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 width 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN width INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    }
    if (!hasHeightColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 height 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN height INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
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

bool DatabaseManager::flushStep() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto stepConn = [](DbConnection& conn) -> bool {
        if (!conn.memDb || !conn.diskDb) return true;
        if (!conn.activeBackup) {
            conn.activeBackup = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
        }
        if (conn.activeBackup) {
            int rc = sqlite3_backup_step(conn.activeBackup, 50); // 1.21：每 50 页一跳
            if (rc == SQLITE_DONE || rc != SQLITE_OK) {
                sqlite3_backup_finish(conn.activeBackup);
                conn.activeBackup = nullptr;
                return true;
            }
            return false;
        }
        return true;
    };

    bool allDone = true;
    if (!stepConn(m_globalDb)) allDone = false;
    for (auto& pair : m_driveDbs) {
        if (!stepConn(pair.second)) allDone = false;
    }
    return allDone;
}

void DatabaseManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 强制完成所有挂起的备份
    auto forceFinish = [](DbConnection& conn) {
        if (conn.activeBackup) {
            sqlite3_backup_step(conn.activeBackup, -1);
            sqlite3_backup_finish(conn.activeBackup);
            conn.activeBackup = nullptr;
        } else {
            // 如果没有活动备份，执行一次完整的同步
            if (conn.memDb && conn.diskDb) {
                sqlite3_backup* b = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
                if (b) {
                    sqlite3_backup_step(b, -1);
                    sqlite3_backup_finish(b);
                }
            }
        }
    };
    
    forceFinish(m_globalDb);
    for (auto& pair : m_driveDbs) forceFinish(pair.second);

    // 关闭所有句柄 (1.21：解除物理占用)
    for (auto& pair : m_driveDbs) {
        if (pair.second.memDb) sqlite3_close_v2(pair.second.memDb);
        if (pair.second.diskDb) sqlite3_close_v2(pair.second.diskDb);
        pair.second.memDb = nullptr;
        pair.second.diskDb = nullptr;
    }
    if (m_globalDb.memDb) sqlite3_close_v2(m_globalDb.memDb);
    if (m_globalDb.diskDb) sqlite3_close_v2(m_globalDb.diskDb);
    m_globalDb.memDb = nullptr;
    m_globalDb.diskDb = nullptr;
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

sqlite3* DatabaseManager::openScopedDb(const std::wstring& dbPath) {
    // 2026-07-xx 按照 Plan-58：文件夹专属数据库加载。
    // 注意：此类数据库不进入全局 m_driveDbs 管理，由调用方负责生命周期。
    sqlite3* db = nullptr;
    std::string utf8Path = QString::fromStdWString(dbPath).toUtf8().toStdString();

    if (sqlite3_open_v2(utf8Path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
        // 物理同步：初始化 Scoped DB 表结构（复用主库 Schema 定义）
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
                is_invalid INTEGER DEFAULT 0,
                width INTEGER DEFAULT 0,
                height INTEGER DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);

            -- 系统统计表，用于记录 Scan_Timestamp
            CREATE TABLE IF NOT EXISTS system_stats (
                key TEXT PRIMARY KEY,
                value TEXT
            );
        )";
        sqlite3_exec(db, schema, nullptr, nullptr, nullptr);
        return db;
    }

    if (db) sqlite3_close(db);
    return nullptr;
}

void DatabaseManager::closeScopedDb(sqlite3* db) {
    if (db) {
        sqlite3_close(db);
    }
}

bool DatabaseManager::exportToScopedDb(const std::wstring& dbPath, const std::vector<ItemRecord>& records, long long scanTimestamp) {
    sqlite3* db = openScopedDb(dbPath);
    if (!db) return false;

    char* errMsg = nullptr;
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);

    // 1. 写入元数据记录
    const char* sql = "INSERT OR REPLACE INTO metadata (file_id, path, is_folder, rating, color, tags, note, url, ctime, mtime, atime, file_size, palettes, is_trash, original_path, is_invalid, width, height) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& r : records) {
            sqlite3_bind_text(stmt, 1, r.fileId.toStdString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 2, r.path.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, r.isDir ? 1 : 0);
            sqlite3_bind_int(stmt, 4, r.rating);
            sqlite3_bind_text16(stmt, 5, r.color.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 6, r.tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 7, r.note.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(stmt, 8, r.url.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 9, r.ctime);
            sqlite3_bind_int64(stmt, 10, r.mtime);
            sqlite3_bind_int64(stmt, 11, r.atime);
            sqlite3_bind_int64(stmt, 12, r.size);

            QJsonArray arr;
            for (const auto& pe : r.palettes) {
                QJsonObject obj;
                obj["color"] = pe.color.name();
                obj["ratio"] = (double)pe.ratio;
                arr.append(obj);
            }
            QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
            sqlite3_bind_blob(stmt, 13, ba.constData(), ba.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 14, r.isTrash ? 1 : 0);
            sqlite3_bind_text16(stmt, 15, r.originalPath.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 16, r.isInvalid ? 1 : 0);
            sqlite3_bind_int(stmt, 17, r.width);
            sqlite3_bind_int(stmt, 18, r.height);

            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }

    // 2. 写入扫描时间戳
    const char* tsSql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES ('Scan_Timestamp', ?)";
    if (sqlite3_prepare_v2(db, tsSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, QString::number(scanTimestamp).toStdString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return true;
}

} // namespace ArcMeta
