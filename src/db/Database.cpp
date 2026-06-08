#include "Database.h"
#include "../util/PathManager.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <mutex>

namespace ArcMeta {

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_instances) {
        if (pair.second->isDirty) {
            performBackupInternal(pair.second, true);
        }
    }
}

bool Database::init() {
    std::wstring globalDiskPath = PathManager::getGlobalDbPath();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_instances.count(L"")) return true;

    auto inst = std::make_shared<DbInstance>();
    inst->volSerial = L"";
    inst->diskPath = globalDiskPath;
    inst->memoryUri = L"file:global_mem?mode=memory&cache=shared";
    inst->backupTimer = new QTimer(this);
    inst->backupTimer->setInterval(1500);
    inst->backupTimer->setSingleShot(true);
    inst->backupTimer->setProperty("volSerial", QString::fromStdWString(L""));
    connect(inst->backupTimer, &QTimer::timeout, this, &Database::onBackupTimeout);

    m_instances[L""] = inst;

    // 1. 初始化磁盘库（使用独立连接）
    {
        QString initConn = "init_global_disk";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", initConn);
        db.setDatabaseName(QString::fromStdWString(globalDiskPath));
        if (db.open()) {
            createTables(db, true);
            createIndexes(db, true);
            db.close();
        }
        QSqlDatabase::removeDatabase(initConn);
        PathManager::hideFile(globalDiskPath);
    }

    // 2. 加载到内存
    return performBackupInternal(inst, false);
}

bool Database::mountVolume(const std::wstring& volSerial) {
    if (volSerial.empty()) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_instances.count(volSerial)) return true;

    std::wstring diskPath = PathManager::getVolumeDbPath(volSerial);
    
    auto inst = std::make_shared<DbInstance>();
    inst->volSerial = volSerial;
    inst->diskPath = diskPath;
    inst->memoryUri = L"file:mem_" + volSerial + L"?mode=memory&cache=shared";
    inst->backupTimer = new QTimer(this);
    inst->backupTimer->setInterval(1500);
    inst->backupTimer->setSingleShot(true);
    inst->backupTimer->setProperty("volSerial", QString::fromStdWString(volSerial));
    connect(inst->backupTimer, &QTimer::timeout, this, &Database::onBackupTimeout);

    m_instances[volSerial] = inst;

    // 初始化磁盘库
    {
        QString initConn = "init_vol_disk_" + QString::fromStdWString(volSerial);
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", initConn);
        db.setDatabaseName(QString::fromStdWString(diskPath));
        if (db.open()) {
            createTables(db, false);
            createIndexes(db, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(initConn);
        PathManager::hideFile(diskPath);
    }

    return performBackupInternal(inst, false);
}

void Database::unmountVolume(const std::wstring& volSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_instances.count(volSerial)) return;

    auto inst = m_instances[volSerial];
    if (inst->isDirty) {
        performBackupInternal(inst, true);
    }
    m_instances.erase(volSerial);
}

std::vector<std::wstring> Database::getMountedVolumes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::wstring> result;
    for (const auto& pair : m_instances) {
        if (!pair.first.empty()) result.push_back(pair.first);
    }
    return result;
}

QSqlDatabase Database::getThreadDatabase(const std::wstring& volSerial) {
    QString connName = QString("conn_%1_%2").arg((quintptr)QThread::currentThreadId()).arg(QString::fromStdWString(volSerial));
    
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::database(connName);
        if (db.isOpen()) return db;
    }

    std::shared_ptr<DbInstance> inst;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_instances.find(volSerial);
        if (it == m_instances.end()) {
            if (volSerial.empty()) {
                m_mutex.unlock();
                init();
                m_mutex.lock();
                inst = m_instances[L""];
            } else {
                return QSqlDatabase();
            }
        } else {
            inst = it->second;
        }
    }

    if (!inst) return QSqlDatabase();

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(QString::fromStdWString(inst->memoryUri));

    if (!db.open()) {
        qCritical() << "[Database] 无法打开内存库:" << db.lastError().text();
    }
    return db;
}

void Database::markDirty(const std::wstring& volSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_instances.count(volSerial)) {
        auto inst = m_instances[volSerial];
        inst->isDirty = true;
        QMetaObject::invokeMethod(inst->backupTimer, "start", Qt::QueuedConnection);
    }
}

void Database::onBackupTimeout() {
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;
    std::wstring volSerial = timer->property("volSerial").toString().toStdWString();

    std::shared_ptr<DbInstance> inst;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_instances.count(volSerial)) inst = m_instances[volSerial];
    }
    if (inst) performBackupInternal(inst, true);
}

bool Database::performBackupInternal(std::shared_ptr<DbInstance> inst, bool toDisk) {
    // 由于环境缺少 sqlite3.h，改用 SQL 指令方式
    // 逻辑：如果是 toDisk，则将内存库的内容写入磁盘库；如果是 fromDisk，则读取磁盘库内容到内存库
    QString tempConn = QString("backup_op_%1").arg((quintptr)QThread::currentThreadId());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", tempConn);
    db.setDatabaseName(QString::fromStdWString(inst->memoryUri));

    if (!db.open()) return false;

    QSqlQuery q(db);
    // 挂载磁盘库
    QString attachSql = QString("ATTACH DATABASE '%1' AS disk").arg(QString::fromStdWString(inst->diskPath));
    if (!q.exec(attachSql)) {
        qCritical() << "[Database] 备份挂载失败:" << q.lastError().text();
        QSqlDatabase::removeDatabase(tempConn);
        return false;
    }

    bool ok = true;
    if (toDisk) {
        // Memory -> Disk: 清空磁盘表，插入内存表内容
        // 这里需要针对 global 或 vol 库执行不同的同步指令
        bool isGlobal = inst->volSerial.empty();
        db.transaction();
        if (isGlobal) {
            q.exec("DELETE FROM disk.categories");
            q.exec("INSERT INTO disk.categories SELECT * FROM main.categories");
            q.exec("DELETE FROM disk.category_items");
            q.exec("INSERT INTO disk.category_items SELECT * FROM main.category_items");
            q.exec("DELETE FROM disk.tags");
            q.exec("INSERT INTO disk.tags SELECT * FROM main.tags");
        } else {
            q.exec("DELETE FROM disk.files");
            q.exec("INSERT INTO disk.files SELECT * FROM main.files");
            q.exec("DELETE FROM disk.palettes");
            q.exec("INSERT INTO disk.palettes SELECT * FROM main.palettes");
        }
        ok = db.commit();
    } else {
        // Disk -> Memory
        bool isGlobal = inst->volSerial.empty();
        db.transaction();
        if (isGlobal) {
            q.exec("DELETE FROM main.categories");
            q.exec("INSERT INTO main.categories SELECT * FROM disk.categories");
            q.exec("DELETE FROM main.category_items");
            q.exec("INSERT INTO main.category_items SELECT * FROM disk.category_items");
            q.exec("DELETE FROM main.tags");
            q.exec("INSERT INTO main.tags SELECT * FROM disk.tags");
        } else {
            q.exec("DELETE FROM main.files");
            q.exec("INSERT INTO main.files SELECT * FROM disk.files");
            q.exec("DELETE FROM main.palettes");
            q.exec("INSERT INTO main.palettes SELECT * FROM disk.palettes");
        }
        ok = db.commit();
    }

    q.exec("DETACH DATABASE disk");
    db.close();
    QSqlDatabase::removeDatabase(tempConn);

    if (toDisk && ok) {
        inst->isDirty = false;
        PathManager::hideFile(inst->diskPath);
    }

    return ok;
}

void Database::createTables(QSqlDatabase& db, bool isGlobal) {
    QSqlQuery q(db);
    if (isGlobal) {
        q.exec("CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER DEFAULT 0, name TEXT NOT NULL, color TEXT DEFAULT '', preset_tags TEXT DEFAULT '', sort_order INTEGER DEFAULT 0, pinned INTEGER DEFAULT 0, encrypted INTEGER DEFAULT 0, encrypt_hint TEXT DEFAULT '', created_at REAL)");
        q.exec("CREATE TABLE IF NOT EXISTS category_items (category_id INTEGER, file_id_128 TEXT, vol_serial TEXT, added_at REAL, PRIMARY KEY (category_id, file_id_128, vol_serial))");
        q.exec("CREATE TABLE IF NOT EXISTS tags (tag TEXT PRIMARY KEY, item_count INTEGER DEFAULT 0)");
        q.exec("CREATE TABLE IF NOT EXISTS favorites (path TEXT PRIMARY KEY, type TEXT, name TEXT, sort_order INTEGER DEFAULT 0, added_at REAL)");
    } else {
        q.exec("CREATE TABLE IF NOT EXISTS files (file_id_128 TEXT PRIMARY KEY, frn TEXT, path TEXT, parent_path TEXT, type TEXT, rating INTEGER DEFAULT 0, color TEXT DEFAULT '', tags TEXT DEFAULT '', pinned INTEGER DEFAULT 0, note TEXT DEFAULT '', url TEXT DEFAULT '', size INTEGER DEFAULT 0, ctime REAL DEFAULT 0, mtime REAL DEFAULT 0, atime REAL DEFAULT 0, deleted INTEGER DEFAULT 0)");
        q.exec("CREATE TABLE IF NOT EXISTS palettes (file_id_128 TEXT, r INTEGER, g INTEGER, b INTEGER, ratio REAL)");
    }
}

void Database::createIndexes(QSqlDatabase& db, bool isGlobal) {
    QSqlQuery q(db);
    if (!isGlobal) {
        q.exec("CREATE INDEX IF NOT EXISTS idx_files_path ON files(path)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_files_frn ON files(frn)");
    }
}

} // namespace ArcMeta
