#include "NativeFolderWatcher.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <algorithm>

namespace ArcMeta {

NativeFolderWatcher& NativeFolderWatcher::instance() {
    static NativeFolderWatcher inst;
    return inst;
}

NativeFolderWatcher::NativeFolderWatcher(QObject* parent) 
    : QObject(parent), m_hIOCP(INVALID_HANDLE_VALUE), m_running(true) {
    
    // QTimer 线程安全校验，必须确保在主线程中实例化
    Q_ASSERT_X(QThread::currentThread() == QCoreApplication::instance()->thread(),
               "NativeFolderWatcher::instance",
               "NativeFolderWatcher must be initialized in the main GUI thread!");

    m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    
    // 初始化 20ms 事件合并与去重定时器
    m_mergeTimer = new QTimer(this);
    m_mergeTimer->setInterval(20); // 20ms 高速轻量分发
    connect(m_mergeTimer, &QTimer::timeout, this, &NativeFolderWatcher::processRawEvents);
    m_mergeTimer->start();

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

    if (m_mergeTimer) {
        m_mergeTimer->stop();
    }

    m_pendingOldNames.clear();
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
    if (bytesTransferred == 0) {
        qWarning() << "[Watcher] 检测到监控缓冲区溢出（变更信号极其密集），启动全量级联扫描自愈对账...";
        std::wstring folderPath = item->path;
        emit bufferOverflowed(folderPath);
        return;
    }

    BYTE* pBase = item->buffer;
    QDateTime now = QDateTime::currentDateTime();

    std::vector<FileEvent> batchEvents;

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

        FileEvent ev;
        ev.timestamp = now;
        ev.path = qFullPath;

        if (notify->Action == FILE_ACTION_ADDED) {
            ev.action = FileAction::Added;
            batchEvents.push_back(ev);
        } else if (notify->Action == FILE_ACTION_MODIFIED) {
            ev.action = FileAction::Modified;
            batchEvents.push_back(ev);
        } else if (notify->Action == FILE_ACTION_REMOVED) {
            ev.action = FileAction::Removed;
            batchEvents.push_back(ev);
        } else if (notify->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            ev.action = FileAction::Renamed;
            ev.oldPath = qFullPath; // 暂时只记录旧路径
            batchEvents.push_back(ev);
        } else if (notify->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            // 如果在同一个缓冲区里，前一个恰好是 Renamed (OLD)，我们可以就地配对
            if (!batchEvents.empty() && batchEvents.back().action == FileAction::Renamed && batchEvents.back().path.isEmpty()) {
                batchEvents.back().path = qFullPath;
            } else {
                ev.action = FileAction::Renamed;
                // path 填充，oldPath 留空，后续由合并池统一按时间匹配
                ev.path = qFullPath;
                batchEvents.push_back(ev);
            }
        }

        if (notify->NextEntryOffset == 0) break;
        pBase += notify->NextEntryOffset;
    }

    // 极速汇入高速原始队列
    if (!batchEvents.empty()) {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        m_rawEventQueue.insert(m_rawEventQueue.end(), batchEvents.begin(), batchEvents.end());
    }
}

void NativeFolderWatcher::processRawEvents() {
    std::vector<FileEvent> events;
    bool queueEmpty = false;
    {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        queueEmpty = m_rawEventQueue.empty();
        if (!queueEmpty) {
            events = std::move(m_rawEventQueue);
            m_rawEventQueue.clear();
        }
    }

    // 只有当新事件队列和待配对暂存池均为空时，才安全退出
    if (queueEmpty && m_pendingOldNames.empty()) {
        return;
    }

    QDateTime now = QDateTime::currentDateTime();

    // 智能配对与去重引擎
    // A. 解决并发重命名配对问题：
    // 在同批次或相邻批次的 IO 变动中，将带有 oldPath 的 Renamed 和 带有 path 却无 oldPath 的 Renamed 进行最优时间匹配。
    for (auto it = events.begin(); it != events.end(); ) {
        if (it->action == FileAction::Renamed) {
            if (!it->oldPath.isEmpty() && it->path.isEmpty()) {
                // OLD_NAME：存入未配对列表
                m_pendingOldNames.push_back({it->oldPath, it->timestamp});
                it = events.erase(it);
            } else if (it->oldPath.isEmpty() && !it->path.isEmpty()) {
                // NEW_NAME：在 pendingOldNames 中搜寻发生时间最近的旧路径进行匹配
                int bestIdx = -1;
                qint64 minDiff = 1000; // 1秒内的最大允许延迟

                for (size_t i = 0; i < m_pendingOldNames.size(); ++i) {
                    qint64 diff = std::abs(m_pendingOldNames[i].timestamp.msecsTo(it->timestamp));
                    if (diff < minDiff) {
                        minDiff = diff;
                        bestIdx = static_cast<int>(i);
                    }
                }

                if (bestIdx != -1) {
                    it->oldPath = m_pendingOldNames[bestIdx].oldPath;
                    m_pendingOldNames.erase(m_pendingOldNames.begin() + bestIdx);
                    ++it;
                } else {
                    // 无对应的 OLD_NAME，作废为 Added 信号
                    it->action = FileAction::Added;
                    ++it;
                }
            } else {
                // 已就地配对好的 Renamed
                ++it;
            }
        } else {
            ++it;
        }
    }

    // B. 超时未配对的 OLD_NAME 认为被物理清退 (删除)，延迟 50ms 校验触发
    // 在本周期内，检查 m_pendingOldNames 中是否有超过 50ms 依然无法配对的项，如果过期，直接生成 Removed 信号。
    for (auto it = m_pendingOldNames.begin(); it != m_pendingOldNames.end(); ) {
        if (it->timestamp.msecsTo(now) >= 50) {
            FileEvent ev;
            ev.action = FileAction::Removed;
            ev.path = it->oldPath;
            ev.timestamp = now;
            events.push_back(ev);

            it = m_pendingOldNames.erase(it);
        } else {
            ++it;
        }
    }

    // C. 状态折叠与极速去风暴 (对同一文件同一类型的信号进行批次内去重)
    // 如果短时间内对同一文件有多次 Modified、多次 Added，直接去重合并。
    // 如果既有 Added 又有 Modified，合并为 Added。
    // 如果有 Removed，则清除同一文件的 Added/Modified。
    std::map<QString, FileEvent> merged;
    std::vector<FileEvent> orderedRenamed; // 重命名需要保持顺序，不放入合并图

    for (const auto& ev : events) {
        if (ev.action == FileAction::Renamed) {
            orderedRenamed.push_back(ev);
            continue;
        }

        auto it = merged.find(ev.path);
        if (it == merged.end()) {
            merged[ev.path] = ev;
        } else {
            // 合并决策
            if (ev.action == FileAction::Removed) {
                merged[ev.path] = ev;
            } else if (ev.action == FileAction::Added) {
                merged[ev.path] = ev;
            } else if (ev.action == FileAction::Modified) {
                if (it->second.action != FileAction::Added) {
                    it->second = ev;
                }
            }
        }
    }

    // D. 顺序发送信号给主线程
    // 1. 发送合并后的常规信号
    for (const auto& pair : merged) {
        const auto& ev = pair.second;
        if (ev.action == FileAction::Added) {
            emit fileAdded(ev.path);
        } else if (ev.action == FileAction::Modified) {
            emit fileModified(ev.path);
        } else if (ev.action == FileAction::Removed) {
            std::wstring wPath = ev.path.toStdWString();
            emit managedFolderRemoved(wPath);
            emit fileRemoved(ev.path);
        }
    }

    // 2. 发送重命名信号
    for (const auto& ev : orderedRenamed) {
        if (!ev.oldPath.isEmpty() && !ev.path.isEmpty()) {
            emit fileRenamed(ev.oldPath, ev.path);
        }
    }
}

} // namespace ArcMeta
