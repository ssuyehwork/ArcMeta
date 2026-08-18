#include "NativeFolderWatcher.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QtConcurrent>

namespace ArcMeta {

NativeFolderWatcher& NativeFolderWatcher::instance() {
    static NativeFolderWatcher inst;
    return inst;
}

NativeFolderWatcher::NativeFolderWatcher(QObject* parent) 
    : QObject(parent), m_hIOCP(INVALID_HANDLE_VALUE), m_running(true) {
    
    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    
    // 初始化 100ms 批次定时器，必须关联主线程事件循环
    m_batchTimer = new QTimer(this);
    m_batchTimer->setInterval(100); // 100ms 聚合分批发送
    connect(m_batchTimer, &QTimer::timeout, this, &NativeFolderWatcher::processBatchQueue);
    m_batchTimer->start();

    // 启动线程池 (根据 CPU 核心数)
    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;
    qDebug() << "[Watcher] 初始化高吞吐量 IOCP 监控，启动工作线程数:" << threads;
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

    auto item = std::make_shared<WatchItem>();
    item->hDir = hDir;
    item->path = path;

    if (!CreateIoCompletionPort(hDir, m_hIOCP, (ULONG_PTR)item.get(), 0)) {
        qWarning() << "[Watcher] CreateIoCompletionPort 关联失败! Error:" << GetLastError();
        return;
    }

    m_watches[path] = item;
    m_outstandingWatches[item.get()] = item;
    qDebug() << "[Watcher] IOCP 关联成功，句柄:" << hDir;
    
    requestChanges(item);
    qDebug() << "[Watcher] 监控已就绪:" << QString::fromStdWString(path);
}

void NativeFolderWatcher::removeWatch(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_watches.find(path);
    if (it != m_watches.end()) {
        std::shared_ptr<WatchItem> item = it->second;
        // 只从活跃映射中擦除
        m_watches.erase(it);
        
        // 取消挂起的异步 I/O 动作。注意：此操作可能会在 IOCP 中产生一个完成通知包，
        // 我们需要保持 shared_ptr 存在于 m_outstandingWatches 中，直至完成包被释放。
        CancelIoEx(item->hDir, &item->overlapped);
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

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watches.clear();
        m_outstandingWatches.clear();

        if (m_hIOCP != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hIOCP);
            m_hIOCP = INVALID_HANDLE_VALUE;
        }
    }
}

void NativeFolderWatcher::requestChanges(std::shared_ptr<WatchItem> item) {
    if (!m_running) return;

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
        if (!ok && overlapped == NULL) continue; // 真正的系统错误

        // 在进行任何指针操作前，必须加锁，使用完成键在 m_outstandingWatches 进行高效检索
        std::shared_ptr<WatchItem> activeItem;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            WatchItem* rawItem = (WatchItem*)completionKey;
            auto it = m_outstandingWatches.find(rawItem);
            if (it != m_outstandingWatches.end()) {
                activeItem = it->second;
            }
        }

        // 如果对应的监控项已经被完全清场释放，或者 completionKey 为空，则跳过
        if (!activeItem) {
            continue;
        }

        DWORD err = ok ? 0 : GetLastError();
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_watches.find(activeItem->path);
            if (it != m_watches.end() && it->second == activeItem) {
                active = true;
            }
        }

        // 🚨 优先保障取消包 / 撤销通知无阻碍执行：直接进行清除释放
        if (err == ERROR_OPERATION_ABORTED || !active) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_outstandingWatches.erase(activeItem.get());
            activeItem->isProcessing = false;
            continue;
        }

        // 线程加锁逻辑：原子 CAS 锁，锁定 activeItem，防止同一个 item 在不同工作线程中并发冲突处理
        bool expected = false;
        if (!activeItem->isProcessing.compare_exchange_strong(expected, true)) {
            // 发生竞争，说明另一个线程已经并行的进入了解析阶段。跳过，防止发生内存并发竞争
            continue;
        }

        handleNotification(activeItem, bytesTransferred);

        // 重设 processing 状态
        activeItem->isProcessing = false;

        // 解析完成后，若该 item 依然处于活跃监控列表中，则重新发起 ReadDirectoryChangesW
        bool stillActive = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_watches.find(activeItem->path);
            if (it != m_watches.end() && it->second == activeItem) {
                stillActive = true;
            }
        }
        
        if (stillActive) {
            requestChanges(activeItem);
        } else {
            // 🚨 完备生命周期清理：若此监控已失效，必须主动在 outstanding 集合中清除，规避句柄和内存泄漏！
            std::lock_guard<std::mutex> lock(m_mutex);
            m_outstandingWatches.erase(activeItem.get());
        }
    }
}

void NativeFolderWatcher::handleNotification(std::shared_ptr<WatchItem> item, DWORD bytesTransferred) {
    // 拦截 3. 缓冲区溢出时的自适应去噪标记（解决缺陷 3）
    if (bytesTransferred == 0) {
        qWarning() << "[Watcher] 检测到监控缓冲区溢出（变更信号极其密集），加入批次缓冲稍后在主线程弹性自愈对账...";
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_rawEvents.push_back({0, QString::fromStdWString(item->path)}); // Action = 0 代表溢出标记
        return;
    }

    BYTE* pBase = item->buffer;
    std::vector<RawEvent> batchLocal;

    while (true) {
        FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)pBase;
        std::wstring fileName(notify->FileName, notify->FileNameLength / sizeof(WCHAR));
        
        // 统一使用 Windows 原生分隔符拼接路径，并确保格式标准化
        QString qFullPath = QString::fromStdWString(item->path);
        qFullPath.append("/");
        qFullPath.append(QString::fromStdWString(fileName));
        qFullPath = QDir::toNativeSeparators(qFullPath);

        // 过滤规则：严禁监控 .arcmeta 目录自身的变动，防止死循环
        if (qFullPath.contains("/.arcmeta") || qFullPath.contains("\\.arcmeta")) {
            if (notify->NextEntryOffset == 0) break;
            pBase += notify->NextEntryOffset;
            continue;
        }

        batchLocal.push_back({(int)notify->Action, qFullPath});

        if (notify->NextEntryOffset == 0) break;
        pBase += notify->NextEntryOffset;
    }

    // 批量无锁/极低锁竞争写入主线程缓冲区
    if (!batchLocal.empty()) {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_rawEvents.insert(m_rawEvents.end(), batchLocal.begin(), batchLocal.end());
    }
}

void NativeFolderWatcher::processBatchQueue() {
    std::vector<RawEvent> eventsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        if (m_rawEvents.empty() && m_renamePool.empty()) {
            return;
        }
        eventsToProcess.swap(m_rawEvents);
    }

    QList<FileWatcherEvent> finalizedEvents;
    QString lastOldPath;

    // A. 处理溢出、添加、修改、删除和重命名
    for (const auto& raw : eventsToProcess) {
        QString qFullPath = raw.path;
        QFileInfo info(qFullPath);
        bool isDir = info.exists() ? info.isDir() : qFullPath.endsWith("\\") || qFullPath.endsWith("/");

        // 1. 拦截底层溢出
        if (raw.actionType == 0) {
            // 自适应弹性自愈通知：将溢出转换并延迟自适应通知
            FileWatcherEvent ev;
            ev.action = WatcherAction::Modified;
            ev.newPath = qFullPath;
            ev.isDirectory = true; // 告知上层是根目录需要自愈
            finalizedEvents.append(ev);
            continue;
        }

        if (raw.actionType == FILE_ACTION_RENAMED_OLD_NAME) {
            lastOldPath = qFullPath;
            
            // 加入配对池，并记录高精度时间戳
            PendingRename pr;
            pr.timer.start();
            pr.oldPath = qFullPath;
            m_renamePool.push_back(pr);

        } else if (raw.actionType == FILE_ACTION_RENAMED_NEW_NAME) {
            if (!lastOldPath.isEmpty()) {
                // 在同一个包中的相邻项直接高速匹配成功
                QString oldPath = lastOldPath;
                lastOldPath.clear();

                // 移除配对池对应项
                for (auto it = m_renamePool.begin(); it != m_renamePool.end(); ++it) {
                    if (it->oldPath == oldPath) {
                        m_renamePool.erase(it);
                        break;
                    }
                }

                FileWatcherEvent ev;
                ev.action = WatcherAction::Renamed;
                ev.oldPath = oldPath;
                ev.newPath = qFullPath;
                ev.isDirectory = isDir;
                finalizedEvents.append(ev);
            } else {
                // 跨包/跨通知缓冲区，在精确关联池中匹配
                bool matched = false;
                if (!m_renamePool.empty()) {
                    // 进行后缀或名字就近匹配，如果都在 50ms 滑动窗口内
                    for (auto it = m_renamePool.begin(); it != m_renamePool.end(); ++it) {
                        if (it->timer.elapsed() <= 50) {
                            // 优先配对
                            QString oldPath = it->oldPath;
                            m_renamePool.erase(it);

                            FileWatcherEvent ev;
                            ev.action = WatcherAction::Renamed;
                            ev.oldPath = oldPath;
                            ev.newPath = qFullPath;
                            ev.isDirectory = isDir;
                            finalizedEvents.append(ev);
                            matched = true;
                            break;
                        }
                    }
                }

                if (!matched) {
                    // 无法在滑窗内配对，则当作全新 ADD 处理
                    FileWatcherEvent ev;
                    ev.action = WatcherAction::Added;
                    ev.newPath = qFullPath;
                    ev.isDirectory = isDir;
                    finalizedEvents.append(ev);
                }
            }
        } else if (raw.actionType == FILE_ACTION_ADDED || raw.actionType == FILE_ACTION_MODIFIED) {
            FileWatcherEvent ev;
            ev.action = (raw.actionType == FILE_ACTION_ADDED) ? WatcherAction::Added : WatcherAction::Modified;
            ev.newPath = qFullPath;
            ev.isDirectory = isDir;
            finalizedEvents.append(ev);

        } else if (raw.actionType == FILE_ACTION_REMOVED) {
            FileWatcherEvent ev;
            ev.action = WatcherAction::Removed;
            ev.newPath = qFullPath;
            ev.isDirectory = isDir;
            finalizedEvents.append(ev);
        }
    }

    // B. 超时事务精确结算：遍历重命名映射池，将 50ms 内依然孤立的 OLD_NAME 定性为真正物理删除
    auto it = m_renamePool.begin();
    while (it != m_renamePool.end()) {
        if (it->timer.elapsed() > 50) {
            QString oldPath = it->oldPath;
            it = m_renamePool.erase(it);

            FileWatcherEvent ev;
            ev.action = WatcherAction::Removed;
            ev.newPath = oldPath;
            ev.isDirectory = false; // 默认作文件移除，业务层会有安全判定
            finalizedEvents.append(ev);
        } else {
            ++it;
        }
    }

    // C. 防抖去重合并：采用批次内重合事件压缩
    if (!finalizedEvents.isEmpty()) {
        QList<FileWatcherEvent> compressedEvents;
        QSet<QString> processedPaths;

        // 倒序遍历可以保留最新的最终状态，且去除高频重复的 Modified 事件
        for (int i = finalizedEvents.size() - 1; i >= 0; --i) {
            const auto& ev = finalizedEvents[i];
            QString key = (ev.action == WatcherAction::Renamed) ? (ev.oldPath + "->" + ev.newPath) : ev.newPath;
            if (!processedPaths.contains(key)) {
                processedPaths.insert(key);
                compressedEvents.prepend(ev);
            }
        }

        if (!compressedEvents.isEmpty()) {
            emit filesChanged(compressedEvents);
        }
    }
}

} // namespace ArcMeta
