#ifndef ARCMETA_DATABASE_MANAGER_H
#define ARCMETA_DATABASE_MANAGER_H

#include <QString>
#include <QObject>
#include "sqlite3.h"
#include <map>
#include <string>
#include <mutex>
#include <functional>
#include <vector>
#include "../core/IndexedEntry.h"

struct sqlite3;

namespace ArcMeta {

/**
 * @brief 数据库事务 RAII 守卫
 * 确保即使在逻辑分支提前返回时事务也能安全关闭。
 */
class SqlTransaction {
public:
    explicit SqlTransaction(struct sqlite3* db);
    ~SqlTransaction();

    bool commit();
    void rollback();

private:
    struct sqlite3* m_db;
    bool m_committed = false;
    bool m_isNested = false;
};

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager& instance();

    /**
     * @brief 初始化数据库（加载所有挂载驱动器的数据库到内存）
     */
    bool init();

    /**
     * @brief 持久化所有内存库到磁盘
     */
    void flushAll();

    /**
     * @brief 2026-07-xx 按照用户要求 (1.21)：步进式持久化接口
     * @return 如果所有备份已完成，返回 true；否则返回 false。
     */
    bool flushStep();

    /**
     * @brief 显式关闭并释放所有数据库资源 (1.21)
     */
    void shutdown();

    /**
     * @brief 获取指定磁盘卷序列号对应的内存连接
     * @param volumeSerial 磁盘卷序列号（如 A1B2C3D4）
     */
    sqlite3* getMemoryDb(const std::wstring& volumeSerial);

    /**
     * @brief 获取全局数据库内存连接
     */
    sqlite3* getGlobalDb();

    /**
     * @brief 2026-07-xx 按照 Plan-58：打开文件夹专属数据库 (Scoped DB)
     * @param dbPath 物理数据库路径 (通常位于 {Folder}/.arcmeta/{FID}.db)
     * @return 成功返回 sqlite3 句柄，失败返回 nullptr
     */
    sqlite3* openScopedDb(const std::wstring& dbPath);

    /**
     * @brief 关闭指定的 Scoped DB
     */
    void closeScopedDb(sqlite3* db);

    /**
     * @brief 2026-07-xx 按照 Plan-58：将元数据记录导出到 Scoped DB
     * @param dbPath 目标 Scoped DB 路径
     * @param records 待导出的记录列表
     * @param scanTimestamp 扫描时间戳（用于时效性校验）
     * @return 成功返回 true
     */
    bool exportToScopedDb(const std::wstring& dbPath, const std::vector<ItemRecord>& records, long long scanTimestamp);

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    struct DbConnection {
        sqlite3* diskDb = nullptr;
        sqlite3* memDb = nullptr;
        sqlite3_backup* activeBackup = nullptr;
        std::wstring diskPath;
    };

    std::map<std::wstring, DbConnection> m_driveDbs;
    DbConnection m_globalDb;
    std::mutex m_mutex;

    bool loadDb(const std::wstring& diskPath, DbConnection& conn);
    void saveDb(DbConnection& conn);
    void closeDb(DbConnection& conn);

    QString getAppDir();
    void ensureHidden(const std::wstring& path);
};

} // namespace ArcMeta

#endif // ARCMETA_DATABASE_MANAGER_H
