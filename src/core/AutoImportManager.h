#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <vector>
#include <string>
#include <mutex>
#include <QFutureWatcher>

namespace ArcMeta {

/**
 * @brief 2026-07-xx 按照 Plan-67/68：NTFS 托管文件夹自动入库管理器
 */
class AutoImportManager : public QObject {
    Q_OBJECT
public:
    static AutoImportManager& instance();

    // 针对特定盘符的监听开关
    void setDriveListening(const QString& drive, bool active);

    /**
     * @brief 2026-10-29 按照用户最新要求：采用数据库命名规范构造托管文件夹路径
     * 格式：Arcmeta_[SERIAL]_[LETTER] (例如: D:\Arcmeta_4DFFAF5E_D)
     */
    static QString getManagedLibraryPath(const QString& driveLetter);

    // 任务队列的暂停与恢复
    void setDrivePaused(const QString& drive, bool paused);
    bool isDrivePaused(const QString& drive) const;

    /**
     * @brief 2026-10-29 按照 Plan-105：存量托管文件扫描
     * 基于 MFT 内存索引快速收集路径并批量入库
     */
    void scanManagedLibrary(const QString& drive);

signals:
    // 当某个盘符有新任务进入队列时触发
    void tasksStarted(const QString& drive);
    // 当某个盘符的任务队列处理完成时触发
    void tasksCompleted(const QString& drive);

private slots:
    void onEntryAdded(uint64_t key);
    void onEntryRemoved(uint64_t key);
    void onEntryUpdated(uint64_t key);
    void processImportQueue();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    bool isPathInManagedLibrary(const QString& targetPath, QString& outDrive);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    mutable std::mutex m_mutex;
    
    QSet<QString> m_activeDrives;
    // 缓存已激活盘符的托管库路径前缀 (如 "D:\Arcmeta_4DFFAF5E_D\")，用于秒级前缀匹配
    QHash<QString, QString> m_activeDrivePrefixes; 
    QHash<QString, bool> m_drivePausedMap;
};

} // namespace ArcMeta
