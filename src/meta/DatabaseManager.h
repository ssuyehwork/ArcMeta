#ifndef ARCMETA_DATABASE_MANAGER_H
#define ARCMETA_DATABASE_MANAGER_H

#include <QString>
#include <QObject>
#include "sqlite3.h"
#include <map>
#include <string>
#include <mutex>
#include <functional>

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
     * @brief 显式关闭并释放所有数据库资源 (1.21)
     */
    void shutdown();

    /**
     * @brief 获取指定磁盘卷序列号对应的数据库连接对
     * @return pair<memDb, diskDb>
     */
    std::pair<sqlite3*, sqlite3*> getDualDbs(const std::wstring& volumeSerial, const QString& driveLetter = "");

    /**
     * @brief 获取全局数据库连接对
     */
    std::pair<sqlite3*, sqlite3*> getGlobalDualDbs();

    /**
     * @brief 获取指定磁盘卷序列号对应的内存连接
     */
    sqlite3* getMemoryDb(const std::wstring& volumeSerial, const QString& driveLetter = "");

    /**
     * @brief 获取全局数据库内存连接
     */
    sqlite3* getGlobalDb();

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    struct DbConnection {
        sqlite3* diskDb = nullptr;
        sqlite3* memDb = nullptr;
        std::wstring diskPath;
    };

    std::map<std::wstring, DbConnection> m_driveDbs;
    DbConnection m_globalDb;
    std::mutex m_mutex;

    bool loadDb(const std::wstring& diskPath, DbConnection& conn);
    void closeDb(DbConnection& conn);

    QString getAppDir();
    void ensureHidden(const std::wstring& path);
};

} // namespace ArcMeta

#endif // ARCMETA_DATABASE_MANAGER_H
