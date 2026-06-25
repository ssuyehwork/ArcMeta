#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <vector>
#include <string>
#include <mutex>

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
    // 任务队列的暂停与恢复
    void setDrivePaused(const QString& drive, bool paused);
    bool isDrivePaused(const QString& drive) const;

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

    bool isPathInManagedLibrary(const std::wstring& path, QString& outDrive);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    mutable std::mutex m_mutex;

    QSet<QString> m_activeDrives;
    QHash<QString, bool> m_drivePausedMap;
};

} // namespace ArcMeta
