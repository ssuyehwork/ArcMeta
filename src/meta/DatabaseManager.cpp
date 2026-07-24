#include "DatabaseManager.h"
#include <chrono>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <windows.h>
#include "MetadataManager.h"
#include "../util/ShellHelper.h"
#include "../util/AppDirectoryInitializer.h"

namespace ArcMeta {

SqlTransaction::SqlTransaction(struct sqlite3* db) : m_db(db) {
    DatabaseManager::instance().incrementWriteSources();
    if (m_db) {
        // 2026-07-xx 物理修复 (1.22)：通过检测 autocommit 状态支持伪嵌套事务。
        // 如果 autocommit 为 0，说明已经处于外部事务中。
        m_isNested = (sqlite3_get_autocommit(m_db) == 0);
        
        if (!m_isNested) {
            // 2026-06-xx 物理加固：内置针对 SQLITE_BUSY 的重试机制
            int retry = 0;
            int rc;
            while ((rc = sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr)) == SQLITE_BUSY && retry++ < 5) {
                Sleep(50);
            }
            if (rc != SQLITE_OK) {
                qWarning() << "[DB] 事务开启失败:" << sqlite3_errmsg(m_db);
            }
        }
    }
}

SqlTransaction::~SqlTransaction() {
    if (m_db && !m_committed && !m_isNested) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    DatabaseManager::instance().decrementWriteSources();
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

DatabaseManager::SyncTaskToken::SyncTaskToken() {
    DatabaseManager::instance().incrementPendingTasks();
}

DatabaseManager::SyncTaskToken::SyncTaskToken(SyncTaskToken&& other) noexcept {
    other.m_moved = true;
}

DatabaseManager::SyncTaskToken::~SyncTaskToken() {
    if (!m_moved) {
        DatabaseManager::instance().decrementPendingTasks();
    }
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
    startWorkerThread();

    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(15000);
    connect(m_syncTimer, &QTimer::timeout, this, [this]() {
        enqueueSyncTask([this]() {
            flushAll();
        });
    });
    m_syncTimer->start();
}

DatabaseManager::~DatabaseManager() {
    if (m_syncTimer) {
        m_syncTimer->stop();
    }
    stopWorkerThread();
    flushAll(true);
    for (auto& pair : m_driveDbs) {
        closeDb(pair.second);
    }
    closeDb(m_globalDb);
}

QString DatabaseManager::getAppDir() {
    return QCoreApplication::applicationDirPath();
}

bool DatabaseManager::loadDb(const std::wstring& diskPath, DbConnection& conn) {
    std::string utf8Path = QString::fromStdWString(diskPath).toUtf8().toStdString();
    qDebug() << "[DB] 内存数据库模式开启 ->" << QString::fromStdString(utf8Path);
    
    // 打开独立的磁盘数据库连接
    if (sqlite3_open_v2(utf8Path.c_str(), &conn.diskDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        qDebug() << "[DB] Failed to open disk DB:" << QString::fromStdString(utf8Path);
        return false;
    }
    sqlite3_busy_timeout(conn.diskDb, 25000);

    // 打开独立的内存数据库连接
    if (sqlite3_open_v2(":memory:", &conn.memDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        qDebug() << "[DB] Failed to open memory DB";
        sqlite3_close_v2(conn.diskDb);
        conn.diskDb = nullptr;
        return false;
    }
    sqlite3_busy_timeout(conn.memDb, 25000);
    // 🚀【修改方案一】：彻底删去对 ShellHelper::ensureHidden 的直接耦合，保持 DAL 纯粹性

    // 使用 SQLite Backup API 将 conn.diskDb 的数据一次性导入内存 conn.memDb
    sqlite3_backup* backup = sqlite3_backup_init(conn.memDb, "main", conn.diskDb, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
    } else {
        qWarning() << "[DB] Failed to backup disk to memory:" << sqlite3_errmsg(conn.memDb);
    }

    // 配置高性能 WAL 模式
    sqlite3_exec(conn.diskDb, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(conn.diskDb, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(conn.memDb, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(conn.memDb, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

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
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0,
            ingestion_status INTEGER DEFAULT -1
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
            encrypt_hint TEXT,
            physical_frn INTEGER DEFAULT 0,
            physical_path TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_categories_frn ON categories(physical_frn);

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

        // FTS5 trigram 模糊匹配与自动触发器同步
        const char* ftsSchema = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS metadata_fts USING fts5(
                file_id UNINDEXED,  
                path,  
                tags,  
                note,  
                content='metadata', 
                content_rowid='rowid', 
                tokenize="trigram"
            );
            CREATE TRIGGER IF NOT EXISTS tb_metadata_insert AFTER INSERT ON metadata BEGIN
                INSERT INTO metadata_fts(rowid, file_id, path, tags, note)
                VALUES (new.rowid, new.file_id, new.path, new.tags, new.note);
            END;
            CREATE TRIGGER IF NOT EXISTS tb_metadata_update AFTER UPDATE ON metadata BEGIN
                INSERT INTO metadata_fts(metadata_fts, rowid, file_id, path, tags, note)
                VALUES('delete', old.rowid, old.file_id, old.path, old.tags, old.note);
                INSERT INTO metadata_fts(rowid, file_id, path, tags, note)
                VALUES(new.rowid, new.file_id, new.path, new.tags, new.note);
            END;
            CREATE TRIGGER IF NOT EXISTS tb_metadata_delete AFTER DELETE ON metadata BEGIN
                INSERT INTO metadata_fts(metadata_fts, rowid, file_id, path, tags, note)
                VALUES('delete', old.rowid, old.file_id, old.path, old.tags, old.note);
            END;
        )";
        char* ftsErrMsg = nullptr;
        sqlite3_exec(conn.memDb, ftsSchema, nullptr, nullptr, &ftsErrMsg);
        if (ftsErrMsg) {
            qWarning() << "[DB] FTS Schema error:" << ftsErrMsg;
            sqlite3_free(ftsErrMsg);
        } else {
            // Rebuild FTS index to populate any data loaded from disk
            sqlite3_exec(conn.memDb, "INSERT INTO metadata_fts(metadata_fts) VALUES('rebuild');", nullptr, nullptr, nullptr);
        }
    }

    // 2026-07-xx 物理加固：自动迁移旧版本数据库字段 (Plan-29)
    sqlite3_stmt* checkStmt;
    bool hasWidthColumn = false;
    bool hasHeightColumn = false;
    bool hasIngestionStatusColumn = false;

    if (sqlite3_prepare_v2(conn.memDb, "PRAGMA table_info(metadata)", -1, &checkStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(checkStmt) == SQLITE_ROW) {
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 1));
            if (colName) {
                std::string name(colName);
                if (name == "width") hasWidthColumn = true;
                if (name == "height") hasHeightColumn = true;
                if (name == "ingestion_status") hasIngestionStatusColumn = true;
            }
        }
        sqlite3_finalize(checkStmt);
    }

    if (!hasWidthColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 width 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN width INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    }
    if (!hasHeightColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 height 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN height INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    }
    if (!hasIngestionStatusColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 ingestion_status 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN ingestion_status INTEGER DEFAULT -1", nullptr, nullptr, nullptr);
    }

    // 2026-08-xx 物理同步扩展：迁移 categories 表字段
    sqlite3_stmt* catCheckStmt;
    bool hasFrnColumn = false;
    bool hasPhysicalPathColumn = false;
    bool hasIconColumn = false;
    if (sqlite3_prepare_v2(conn.memDb, "PRAGMA table_info(categories)", -1, &catCheckStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(catCheckStmt) == SQLITE_ROW) {
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(catCheckStmt, 1));
            if (colName) {
                std::string name(colName);
                if (name == "physical_frn") hasFrnColumn = true;
                if (name == "physical_path") hasPhysicalPathColumn = true;
                if (name == "icon") hasIconColumn = true;
            }
        }
        sqlite3_finalize(catCheckStmt);
    }

    if (!hasFrnColumn) {
        sqlite3_exec(conn.memDb, "ALTER TABLE categories ADD COLUMN physical_frn INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
    }
    if (!hasPhysicalPathColumn) {
        sqlite3_exec(conn.memDb, "ALTER TABLE categories ADD COLUMN physical_path TEXT", nullptr, nullptr, nullptr);
    }
    if (!hasIconColumn) {
        sqlite3_exec(conn.memDb, "ALTER TABLE categories ADD COLUMN icon TEXT DEFAULT 'folder_filled'", nullptr, nullptr, nullptr);
    }

    // 2026-08-xx 索引优化
    sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_categories_frn ON categories(physical_frn);", nullptr, nullptr, nullptr);

    conn.diskPath = diskPath;
    return true;
}

void DatabaseManager::saveDb(DbConnection& conn, bool forceFull) {
    if (!conn.diskDb || !conn.memDb) return;

    // 2026-07-20 优化设计：彻底废除性能极低的增量分步备份与频繁 sleep 让步机制。
    // 因为该函数本身就在后台异步工作线程中运行，直接执行一次性全量备份（sqlite3_backup_step 设为 -1）
    // 效率最高，通常在 1-5 毫秒内即可极速完成，完全不需要分片和让路。
    (void)forceFull;
    sqlite3_backup* backup = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
    if (backup) {
        sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
        qDebug() << "[DB] Successfully backed up memory database to disk:" << QString::fromStdWString(conn.diskPath);
    } else {
        qWarning() << "[DB] Failed to initialize backup from memory to disk:" << sqlite3_errmsg(conn.diskDb);
    }
}

void DatabaseManager::closeDb(DbConnection& conn) {
    if (conn.memDb) {
        sqlite3_close_v2(conn.memDb);
    }
    if (conn.diskDb) {
        sqlite3_close_v2(conn.diskDb);
    }
    conn.memDb = nullptr;
    conn.diskDb = nullptr;
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> lock(m_mutex);
    AppDirectoryInitializer::initializeStoragePath(getAppDir());

    QString metaDir = getAppDir() + "/.arcmeta";

    // 加载全局库
    std::wstring globalPath = (metaDir + "/global.db").toStdWString();
    loadDb(globalPath, m_globalDb);

    // 为每个驱动器加载数据库
    // 注意：此处实际应遍历当前在线的驱动器，这里先简化逻辑
    // 实际运行时，MetadataManager 会按需通过 getMemoryDb 触发加载或由 init 调用
    return true;
}

void DatabaseManager::flushAll(bool forceFull) {
    // 24h 滑动窗口 15s 剪枝
    MetadataManager::instance().slideRecentWindow();

    if (!m_isDirty.load()) {
        qDebug() << "[DB] Database is clean (not dirty), skipping flushAll() for instant exit.";
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    saveDb(m_globalDb, forceFull);
    for (auto& pair : m_driveDbs) {
        saveDb(pair.second, forceFull);
    }
    m_isDirty.store(false);
}

bool DatabaseManager::flushStep() {
    // [Plan-130] 秒退架构：彻底废除 flushStep
    return true;
}

void DatabaseManager::shutdown() {
    if (m_syncTimer) {
        m_syncTimer->stop();
    }
    stopWorkerThread();

    flushAll(true);

    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& pair : m_driveDbs) {
        closeDb(pair.second);
    }
    closeDb(m_globalDb);
}

sqlite3* DatabaseManager::getMemoryDb(const std::wstring& volumeSerial, const QString& driveLetter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    qDebug() << "[DB] getMemoryDb requested for Serial:" << QString::fromStdWString(volumeSerial) << "Letter:" << driveLetter;
    
    QString cleanLetter = "";
    if (!driveLetter.isEmpty()) {
        cleanLetter = driveLetter.at(0).toUpper();
    }

    // 2026-07-xx 按照用户要求：若数据库已加载但盘符发生变化，由解耦路由计算新路径
    if (m_driveDbs.find(volumeSerial) != m_driveDbs.end()) {
        if (!cleanLetter.isEmpty()) {
            QString currentDiskPath = QString::fromStdWString(m_driveDbs[volumeSerial].diskPath);
            QString resolvedPath = ShellHelper::resolveAndAlignDatabasePath(volumeSerial, cleanLetter, currentDiskPath, true);
            
            if (currentDiskPath != resolvedPath) {
                qDebug() << "[DB] 检测到盘符漂移并完成物理重对账路由，重建连接中:" << currentDiskPath << " -> " << resolvedPath;
                
                DbConnection& conn = m_driveDbs[volumeSerial];
                saveDb(conn); // 先持久化
                
                // 关闭句柄以解除占用
                if (conn.memDb) sqlite3_close_v2(conn.memDb);
                if (conn.diskDb) sqlite3_close_v2(conn.diskDb);
                conn.memDb = nullptr;
                conn.diskDb = nullptr;

                conn.diskPath = resolvedPath.toStdWString();
                
                // 重新加载到内存
                loadDb(conn.diskPath, conn);
            }
        }
        return m_driveDbs[volumeSerial].memDb;
    }

    // 未加载时的全新加载路由
    QString resolvedPath = ShellHelper::resolveAndAlignDatabasePath(volumeSerial, cleanLetter, "", false);
    DbConnection conn;
    if (loadDb(resolvedPath.toStdWString(), conn)) {
        m_driveDbs[volumeSerial] = conn;
    } else {
        return nullptr;
    }
    return m_driveDbs[volumeSerial].memDb;
}

sqlite3* DatabaseManager::getGlobalDb() {
    return m_globalDb.memDb;
}

std::vector<sqlite3*> DatabaseManager::getActiveMemoryDbs() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<sqlite3*> dbs;
    if (m_globalDb.memDb) dbs.push_back(m_globalDb.memDb);
    for (const auto& pair : m_driveDbs) {
        if (pair.second.memDb) dbs.push_back(pair.second.memDb);
    }
    return dbs;
}

sqlite3* DatabaseManager::getDiskDb(sqlite3* memDb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_globalDb.memDb == memDb) return m_globalDb.diskDb;
    for (auto& pair : m_driveDbs) {
        if (pair.second.memDb == memDb) return pair.second.diskDb;
    }
    return nullptr;
}

std::shared_ptr<std::mutex> DatabaseManager::getDriveMutex(const std::wstring& volSerial) {
    std::lock_guard<std::mutex> lock(m_mapMutex);
    auto it = m_driveDbMutexMap.find(volSerial);
    if (it == m_driveDbMutexMap.end()) {
        auto mtx = std::make_shared<std::mutex>();
        m_driveDbMutexMap[volSerial] = mtx;
        return mtx;
    }
    return it->second;
}

void DatabaseManager::incrementWriteSources() {
    m_activeWriteSources.fetch_add(1);
    m_isDirty.store(true);
}

void DatabaseManager::decrementWriteSources() {
    m_activeWriteSources.fetch_sub(1);
}

void DatabaseManager::incrementPendingTasks() {
    int count = ++m_pendingTasksCount;
    emit pendingTasksCountChanged(count);
}

void DatabaseManager::decrementPendingTasks() {
    int count = --m_pendingTasksCount;
    emit pendingTasksCountChanged(count);
}

void DatabaseManager::enqueueSyncTask(std::function<void()> task) {
    auto token = std::make_shared<SyncTaskToken>(); 
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_syncQueue.push_back([task, token]() {
            task();
        });
    }
    m_queueCv.notify_one();
}

void DatabaseManager::startWorkerThread() {
    m_stopWorker = false;
    m_workerThread = std::thread(&DatabaseManager::workerLoop, this);
}

void DatabaseManager::stopWorkerThread() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopWorker = true;
    }
    m_queueCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void DatabaseManager::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] { return m_stopWorker || !m_syncQueue.empty(); });
            if (m_stopWorker && m_syncQueue.empty()) break;
            task = std::move(m_syncQueue.front());
            m_syncQueue.pop_front();
        }
        if (task) {
            task();
        }
    }
}

} // namespace ArcMeta
