#pragma once

#include <string>
#include <memory>
#include <map>
#include <QSqlDatabase>
#include <QObject>
#include <QTimer>

namespace ArcMeta {

/**
 * @brief 极简隐藏式“一盘一库” SQLite 架构管理器
 * 2026-06-xx 重构：实现 Memory DB + Disk DB 同步，物理隐藏数据库文件
 */
class Database : public QObject {
    Q_OBJECT
public:
    static Database& instance();

    /**
     * @brief 初始化全局数据库 global.db 并建立内存镜像
     */
    bool init();

    /**
     * @brief 挂载指定卷的私有库 vol_XXXX.db 并建立内存镜像
     */
    bool mountVolume(const std::wstring& volSerial);
    
    /**
     * @brief 卸载指定卷，强制触发备份并释放内存
     */
    void unmountVolume(const std::wstring& volSerial);

    /**
     * @brief 获取当前线程专属的数据库连接
     * @param volSerial 如果为空，返回 global.db 的内存连接；否则返回对应卷的内存连接
     */
    QSqlDatabase getThreadDatabase(const std::wstring& volSerial = L"");

    /**
     * @brief 获取所有已挂载卷的序列号
     */
    std::vector<std::wstring> getMountedVolumes() const;

    /**
     * @brief 标记指定库为脏，将在 1.5s 后触发备份
     */
    void markDirty(const std::wstring& volSerial = L"");

private slots:
    void onBackupTimeout();

private:
    Database(QObject* parent = nullptr);
    ~Database() override;

    void createTables(QSqlDatabase& db, bool isGlobal);
    void createIndexes(QSqlDatabase& db, bool isGlobal);

    /**
     * @brief 执行底层 sqlite3_backup
     * @param volSerial 为空代表备份 global.db
     * @param toDisk true: Memory -> Disk, false: Disk -> Memory
     */
    bool performBackup(const std::wstring& volSerial, bool toDisk);

    struct DbInstance {
        std::wstring volSerial; // 为空表示 global.db
        std::wstring diskPath;
        std::wstring memoryUri;
        QTimer* backupTimer = nullptr;
        bool isDirty = false;
    };

    std::map<std::wstring, std::shared_ptr<DbInstance>> m_instances;
    
    // 全局锁，保护 m_instances
    mutable std::mutex m_mutex;

    // 辅助：获取或创建 DbInstance
    std::shared_ptr<DbInstance> getInstance(const std::wstring& volSerial);
};

} // namespace ArcMeta
