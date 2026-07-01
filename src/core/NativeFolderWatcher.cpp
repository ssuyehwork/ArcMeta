#include "NativeFolderWatcher.h"
#include "../meta/MetadataManager.h"
#include <QDebug>
#include <QFileInfo>

namespace ArcMeta {

NativeFolderWatcher& NativeFolderWatcher::instance() {
    static NativeFolderWatcher inst;
    return inst;
}

NativeFolderWatcher::NativeFolderWatcher(QObject* parent)
    : QObject(parent), m_hIOCP(INVALID_HANDLE_VALUE), m_running(true) {

    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    // 启动线程池 (根据 CPU 核心数)
    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;
    for (unsigned int i = 0; i < threads; ++i) {
        m_workers.emplace_back(&NativeFolderWatcher::workerThread, this);
    }
}

NativeFolderWatcher::~NativeFolderWatcher() {
    shutdown();
}

void NativeFolderWatcher::addWatch(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_watches.count(path)) return;

    HANDLE hDir = CreateFileW(
        path.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        qDebug() << "[Watcher] Failed to open directory:" << QString::fromStdWString(path);
        return;
    }

    WatchItem* item = new WatchItem();
    item->hDir = hDir;
    item->path = path;

    if (!CreateIoCompletionPort(hDir, m_hIOCP, (ULONG_PTR)item, 0)) {
        qDebug() << "[Watcher] Failed to associate with IOCP:" << GetLastError();
        CloseHandle(hDir);
        delete item;
        return;
    }

    m_watches[path] = item;
    requestChanges(item);
    qDebug() << "[Watcher] Started watching:" << QString::fromStdWString(path);
}

void NativeFolderWatcher::removeWatch(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_watches.find(path);
    if (it != m_watches.end()) {
        WatchItem* item = it->second;
        CancelIoEx(item->hDir, &item->overlapped);
        CloseHandle(item->hDir);
        delete item;
        m_watches.erase(it);
    }
}

void NativeFolderWatcher::shutdown() {
    m_running = false;
    if (m_hIOCP != INVALID_HANDLE_VALUE) {
        // 通知所有线程退出
        for (size_t i = 0; i < m_workers.size(); ++i) {
            PostQueuedCompletionStatus(m_hIOCP, 0, 0, NULL);
        }
    }

    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_watches) {
        CloseHandle(pair.second->hDir);
        delete pair.second;
    }
    m_watches.clear();

    if (m_hIOCP != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hIOCP);
        m_hIOCP = INVALID_HANDLE_VALUE;
    }
}

void NativeFolderWatcher::requestChanges(WatchItem* item) {
    ZeroMemory(&item->overlapped, sizeof(OVERLAPPED));
    BOOL success = ReadDirectoryChangesW(
        item->hDir,
        item->buffer,
        sizeof(item->buffer),
        TRUE, // bWatchSubtree = TRUE
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &item->overlapped,
        NULL
    );

    if (!success) {
        qDebug() << "[Watcher] ReadDirectoryChangesW failed:" << GetLastError();
    }
}

void NativeFolderWatcher::workerThread() {
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;

    while (m_running) {
        BOOL ok = GetQueuedCompletionStatus(m_hIOCP, &bytesTransferred, &completionKey, &overlapped, INFINITE);
        if (!m_running) break;
        if (!ok || !completionKey) continue;

        WatchItem* item = (WatchItem*)completionKey;
        handleNotification(item, bytesTransferred);
        requestChanges(item); // 重新发起请求
    }
}

void NativeFolderWatcher::handleNotification(WatchItem* item, DWORD bytesTransferred) {
    if (bytesTransferred == 0) return;

    BYTE* pBase = item->buffer;
    while (true) {
        FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)pBase;
        std::wstring fileName(notify->FileName, notify->FileNameLength / sizeof(WCHAR));
        std::wstring fullPath = item->path + L"/" + fileName;

        // 转换路径分隔符
        for (auto& ch : fullPath) if (ch == L'\\') ch = L'/';

        // 触发 MetadataManager 登记逻辑
        if (notify->Action == FILE_ACTION_ADDED ||
            notify->Action == FILE_ACTION_RENAMED_NEW_NAME ||
            notify->Action == FILE_ACTION_MODIFIED) {

            // 2026-07-xx 按照 Plan-117：触发登记与解析闭环
            // 异步调用以免阻塞工作线程
            QMetaObject::invokeMethod(&MetadataManager::instance(), [fullPath]() {
                MetadataManager::instance().registerItem(fullPath, true);
                // 后续由 registerItem 内部触发解析逻辑
            }, Qt::QueuedConnection);
        }

        if (notify->NextEntryOffset == 0) break;
        pBase += notify->NextEntryOffset;
    }
}

} // namespace ArcMeta
