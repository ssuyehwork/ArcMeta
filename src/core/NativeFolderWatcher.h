#ifndef ARCMETA_NATIVE_FOLDER_WATCHER_H
#define ARCMETA_NATIVE_FOLDER_WATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QSet>
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <set>

namespace ArcMeta {

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
    void managedFolderRemoved(const std::wstring& path);

private slots:
    void processDebounceQueue();
    void enqueueAddOrModify(const QString& path);
    void handleOldName(const QString& oldPath);
    void handleNewName(const QString& newPath);

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

    // 防抖与去重成员
    QTimer* m_debounceTimer;
    QSet<QString> m_debounceAddQueue;

    // 升级为队列/列表容器，杜绝高密集并发/批量重命名时的数据错配与事件丢失
    std::vector<QString> m_pendingRenameOldPaths;

    void workerThread();
    void requestChanges(std::shared_ptr<WatchItem> item);
    void handleNotification(std::shared_ptr<WatchItem> item, DWORD bytesTransferred);
};

} // namespace ArcMeta

#endif // ARCMETA_NATIVE_FOLDER_WATCHER_H
