#pragma once
#include <QObject>
#include <QTimer>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <QFutureWatcher>

namespace ArcMeta {

/**
 * @brief 2026-07-xx 按照 Plan-67/68：NTFS 托管文件夹自动入库管理器
 */
class AutoImportManager : public QObject {
    Q_OBJECT
public:
    static AutoImportManager& instance();

    // 启动/停止监听
    void startListening();
    void stopListening();

    // 2026-07-xx 按照 Plan-97：优先级插队接口
    void setPriorityDrive(const QString& letter);

signals:
    void taskStarted(const QString& letter);
    void taskFinished(const QString& letter);
    void allTasksCompleted();

private slots:
    // 订阅 MftReader 发现的变更
    void onEntryAdded(uint64_t key);
    void onEntryRemoved(uint64_t key);
    
    // 去抖超时，合并写入任务队列
    void processImportQueue();
    
    // 任务调度逻辑
    void scheduleNextTask();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    bool checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder);
    std::wstring getManagedFolderAbsolutePath(const std::wstring& volSerial);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    std::mutex m_queueMutex;
    bool m_isListening = false;

    // 调度器成员
    struct DriveTask {
        QString letter;
        std::vector<std::wstring> paths;
    };
    std::deque<DriveTask> m_taskQueue; // 待处理的盘符任务列表
    DriveTask m_activeTask;            // 当前正在处理的任务
    QFutureWatcher<void> m_taskWatcher;
    std::recursive_mutex m_schedulerMutex;
};

} // namespace ArcMeta
