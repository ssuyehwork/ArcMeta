#include "NativeFolderWatcher.h"
#include "../meta/MetadataManager.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>

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
    qDebug() << "[Watcher] 初始化 IOCP 服务，启动工作线程数:" << threads;
    for (unsigned int i = 0; i < threads; ++i) {
        m_workers.emplace_back(&NativeFolderWatcher::workerThread, this);
    }
}

NativeFolderWatcher::~NativeFolderWatcher() {
    shutdown();
}

void NativeFolderWatcher::addWatch(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_watches.count(path)) {
        qDebug() << "[Watcher] 目录已在监控列表中，跳过:" << QString::fromStdWString(path);
        return;
    }

    qDebug() << "[Watcher] 尝试开启目录监控:" << QString::fromStdWString(path);

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
        qWarning() << "[Watcher] CreateFileW 失败，无法打开目录:" << QString::fromStdWString(path) << "Error:" << GetLastError();
        return;
    }

    WatchItem* item = new WatchItem();
    item->hDir = hDir;
    item->path = path;

    if (!CreateIoCompletionPort(hDir, m_hIOCP, (ULONG_PTR)item, 0)) {
        qWarning() << "[Watcher] CreateIoCompletionPort 关联失败! Error:" << GetLastError();
        CloseHandle(hDir);
        delete item;
        return;
    }

    m_watches[path] = item;
    requestChanges(item);
    qDebug() << "[Watcher] 监控已就绪:" << QString::fromStdWString(path);
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
    qDebug() << "[Watcher] 正在关闭监控服务...";
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
    qDebug() << "[Watcher] 工作线程池已安全退出";

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
        qWarning() << "[Watcher] ReadDirectoryChangesW 发起异步请求失败! Path:" << QString::fromStdWString(item->path) << "Error:" << GetLastError();
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
    if (bytesTransferred == 0) {
        qDebug() << "[Watcher] 收到空通知 (bytesTransferred == 0)";
        return;
    }

    BYTE* pBase = item->buffer;
    while (true) {
        FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)pBase;
        std::wstring fileName(notify->FileName, notify->FileNameLength / sizeof(WCHAR));

        // 统一使用 Windows 原生分隔符拼接路径，并确保格式标准化
        QString qFullPath = QDir::toNativeSeparators(QString::fromStdWString(item->path) + "/" + QString::fromStdWString(fileName));
        std::wstring fullPath = qFullPath.toStdWString();

        qDebug() << "[Watcher] IOCP 收到原始信号 Action:" << notify->Action << "Path:" << qFullPath;

        // 过滤规则：严禁监控 .arcmeta 目录自身的变动，防止死循环
        if (qFullPath.contains("/.arcmeta") || qFullPath.contains("\\.arcmeta")) {
            qDebug() << "[Watcher] 过滤内部数据库变动信号:" << qFullPath;
            if (notify->NextEntryOffset == 0) break;
            pBase += notify->NextEntryOffset;
            continue;
        }

        // 触发 MetadataManager 登记逻辑
        if (notify->Action == FILE_ACTION_ADDED ||
            notify->Action == FILE_ACTION_RENAMED_NEW_NAME ||
            notify->Action == FILE_ACTION_MODIFIED) {

            qDebug() << "[Watcher] 判定为有效变动，准备分发";

            // 2026-07-xx 按照 Plan-117：触发登记与解析闭环
            // 异步调用以免阻塞工作线程
            QMetaObject::invokeMethod(&MetadataManager::instance(), [fullPath]() {
                qDebug() << "[Watcher] 异步回调执行: 开始注册流程" << QString::fromStdWString(fullPath);
                // 优化：如果是文件夹新增，执行批量级联标记
                QFileInfo info(QString::fromStdWString(fullPath));
                if (info.isDir()) {
                    qDebug() << "[Watcher] 检测到目录级变动，触发级联登记";
                    MetadataManager::instance().markAsRegistered(fullPath);
                } else {
                    qDebug() << "[Watcher] 检测到文件级变动，触发单项注册";
                    MetadataManager::instance().registerItem(fullPath, true);
                }
            }, Qt::QueuedConnection);
        } else {
            qDebug() << "[Watcher] 非目标 Action (" << notify->Action << ")，跳过处理";
        }

        if (notify->NextEntryOffset == 0) break;
        pBase += notify->NextEntryOffset;
    }
}

} // namespace ArcMeta
