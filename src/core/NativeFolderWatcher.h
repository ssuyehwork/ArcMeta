#ifndef ARCMETA_NATIVE_FOLDER_WATCHER_H
#define ARCMETA_NATIVE_FOLDER_WATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>

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

private:
    NativeFolderWatcher(QObject* parent = nullptr);
    ~NativeFolderWatcher();

    struct WatchItem {
        HANDLE hDir;
        std::wstring path;
        alignas(DWORD) BYTE buffer[64 * 1024]; // 64KB 缓冲区，确保对齐
        OVERLAPPED overlapped;

        WatchItem() : hDir(INVALID_HANDLE_VALUE) {
            ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        }
    };

    HANDLE m_hIOCP;
    std::map<std::wstring, WatchItem*> m_watches;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running;
    std::mutex m_mutex;

    void workerThread();
    void requestChanges(WatchItem* item);
    void handleNotification(WatchItem* item, DWORD bytesTransferred);
};

} // namespace ArcMeta

#endif // ARCMETA_NATIVE_FOLDER_WATCHER_H
