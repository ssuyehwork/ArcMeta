#include "DatabaseManager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
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
    closeDb(m_scopedDb);
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
    
    // 2026-07-xx 物理同步：如果是新创建的数据库，立即同步一次以写入表结构，防止程序异常退出导致空库
    saveDb(conn);
    
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
    saveDb(m_scopedDb);
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
    if (!stepConn(m_scopedDb)) allDone = false;
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
    forceFinish(m_scopedDb);
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

    if (m_scopedDb.memDb) sqlite3_close_v2(m_scopedDb.memDb);
    if (m_scopedDb.diskDb) sqlite3_close_v2(m_scopedDb.diskDb);
    m_scopedDb.memDb = nullptr;
    m_scopedDb.diskDb = nullptr;
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

sqlite3* DatabaseManager::mountScopedDb(const std::string& folderFid, const std::wstring& folderPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 1. 如果已有挂载的 Scoped DB，先卸载
    if (m_scopedDb.memDb || m_scopedDb.diskDb) {
        closeDb(m_scopedDb);
    }

    // 2. 构造路径: {folderPath}/.arcmeta/{folderFid}.db
    // 物理加固：处理根目录情况 (如 D:\)，防止拼接出 D:\/.arcmeta
    QString root = QString::fromStdWString(folderPath);
    if (!root.endsWith('/') && !root.endsWith('\\')) root += '/';
    QString scopedMetaDir = root + ".arcmeta";
    
    if (!QDir().mkpath(scopedMetaDir)) {
        qDebug() << "[DB] CRITICAL: Failed to create .arcmeta directory at:" << scopedMetaDir;
        return nullptr;
    }
    ensureHidden(scopedMetaDir.toStdWString());

    QString dbName = QString::fromStdString(folderFid);
    // 移除非法字符 (虽然 FID 通常是安全的)
    dbName.replace(":", "_");
    QString dbPath = scopedMetaDir + "/" + dbName + ".db";

    qDebug() << "[DB] Mounting Scoped DB:" << dbPath;
    
    if (loadDb(dbPath.toStdWString(), m_scopedDb)) {
        qDebug() << "[DB] Scoped DB mounted successfully.";
        return m_scopedDb.memDb;
    }
    
    qDebug() << "[DB] Failed to mount Scoped DB at:" << dbPath;
    return nullptr;
}

void DatabaseManager::unmountScopedDb() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_scopedDb.memDb || m_scopedDb.diskDb) {
        qDebug() << "[DB] Unmounting Scoped DB:" << QString::fromStdWString(m_scopedDb.diskPath);
        closeDb(m_scopedDb);
    }
}

sqlite3* DatabaseManager::getScopedDb() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scopedDb.memDb;
}

} // namespace ArcMeta
