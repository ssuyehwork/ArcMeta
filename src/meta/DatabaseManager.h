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
     * @brief 持久化所有库（已废弃，实时落盘模式下为空实现）
     */
    void flushAll();

    /**
     * @brief 步进式持久化接口（已废弃，实时落盘模式下始终返回 true）
     * @return 始终返回 true
     */
    bool flushStep();

    /**
     * @brief 显式关闭并释放所有数据库资源 (1.21)
     */
    void shutdown();

    /**
     * @brief 获取指定磁盘卷序列号对应的数据库连接
     * @param volumeSerial 磁盘卷序列号（如 A1B2C3D4）
     * @param driveLetter 盘符（如 "D" 或 "D:"），可选。若提供则触发数据库文件名自适应重命名。
     */
    sqlite3* getMemoryDb(const std::wstring& volumeSerial, const QString& driveLetter = "");

    /**
     * @brief 获取全局数据库连接
     */
    sqlite3* getGlobalDb();

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    struct DbConnection {
        sqlite3* diskDb = nullptr;
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
