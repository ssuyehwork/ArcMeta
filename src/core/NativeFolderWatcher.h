#ifndef QuarkMeta_NATIVE_FOLDER_WATCHER_H
#define QuarkMeta_NATIVE_FOLDER_WATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QSet>
#include <QElapsedTimer>
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <set>

namespace QuarkMeta {

/**
 * @brief 底层文件监控动作枚举
 */
enum class WatcherAction {
    Added,
    Modified,
    Removed,
    Renamed
};

/**
 * @brief 文件变动通用事件包
 */
struct FileWatcherEvent {
    WatcherAction action;
    QString oldPath; // 仅对 Renamed 动作有效
    QString newPath;
    bool isDirectory;
};

} // namespace QuarkMeta

Q_DECLARE_METATYPE(QuarkMeta::FileWatcherEvent)
Q_DECLARE_METATYPE(QList<QuarkMeta::FileWatcherEvent>)

namespace QuarkMeta {

/**
 * @brief 基于 IOCP + ReadDirectoryChangesW 的高性能异步监控服务
 */
class NativeFolderWatcher : public QObject {
    Q_OBJECT
public:
    static NativeFolderWatcher& instance();

    /**
     * @brief 开始监控指定目录
     * @param path 物理路径
     */
    void addWatch(const std::wstring& path);

    /**
     * @brief 停止监控指定目录
     * @param path 物理路径
     */
    void removeWatch(const std::wstring& path);

    /**
     * @brief 停止所有监控并关闭线程池
     */
    void shutdown();

signals:
    /**
     * @brief 高内聚批次聚合事件推送信号
     * 业务层可通过此信号进行批量去重对账与入库，避免 GUI 线程信号风暴
     */
    void filesChanged(const QList<QuarkMeta::FileWatcherEvent>& events);

    /**
     * @brief 旧版路径清退信号（为了平滑向后兼容保留，亦可在桥接处同步）
     */
    void managedFolderRemoved(const std::wstring& path);

private slots:
    /**
     * @brief 定时分批管道逻辑
     */
    void processBatchQueue();

private:
    NativeFolderWatcher(QObject* parent = nullptr);
    ~NativeFolderWatcher();

    struct WatchItem {
        HANDLE hDir;
        std::wstring path;
        alignas(DWORD) BYTE buffer[64 * 1024]; // 64KB 缓冲区，确保对齐
        OVERLAPPED overlapped;
        std::atomic<bool> isProcessing;

        WatchItem() : hDir(INVALID_HANDLE_VALUE), isProcessing(false) {
            ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        }
        ~WatchItem() {
            if (hDir != INVALID_HANDLE_VALUE) {
                CloseHandle(hDir);
                hDir = INVALID_HANDLE_VALUE;
            }
        }
    };

    HANDLE m_hIOCP;
    std::map<std::wstring, std::shared_ptr<WatchItem>> m_watches;
    
    // 提升为红黑树/哈希映射，实现 O(1)/O(log N) 高效检索与防死锁设计
    std::map<WatchItem*, std::shared_ptr<WatchItem>> m_outstandingWatches; 
    
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running;
    std::mutex m_mutex;

    // 定时分批与防抖缓冲容器
    QTimer* m_batchTimer;
    std::mutex m_eventMutex;
    
    // 原始工作线程投递事件包缓冲区
    struct RawEvent {
        int actionType; // Windows FILE_ACTION_*
        QString path;
    };
    std::vector<RawEvent> m_rawEvents;

    // 超时事务精确关联池 (带滑窗超时判定)
    struct PendingRename {
        QElapsedTimer timer;
        QString oldPath;
    };
    std::vector<PendingRename> m_renamePool;

    void workerThread();
    void requestChanges(std::shared_ptr<WatchItem> item);
    void handleNotification(std::shared_ptr<WatchItem> item, DWORD bytesTransferred);
};

} // namespace QuarkMeta

#endif // QuarkMeta_NATIVE_FOLDER_WATCHER_H
