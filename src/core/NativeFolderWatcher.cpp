#include "NativeFolderWatcher.h"
#include "../meta/MetadataManager.h"
#include "AutoImportManager.h"
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
    
    // 初始化防抖定时器，必须关联主线程事件循环
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(200); // 200ms 防抖滑窗
    connect(m_debounceTimer, &QTimer::timeout, this, &NativeFolderWatcher::processDebounceQueue);

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
    m_outstandingWatches.insert(item);
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

        // 在进行任何指针操作前，必须加锁，验证 completionKey 对应的 WatchItem 是否仍有效存活
        std::shared_ptr<WatchItem> activeItem;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            WatchItem* rawItem = (WatchItem*)completionKey;
            for (const auto& item : m_outstandingWatches) {
                if (item.get() == rawItem) {
                    activeItem = item;
                    break;
                }
            }
        }

        // 如果对应的监控项已经被释放（非 outstanding），或者 completionKey 为空，则跳过
        if (!activeItem) {
            continue;
        }

        // 线程加锁逻辑：避免同一个 WatchItem 的多线程并发重入解析
        bool expected = false;
        if (!activeItem->isProcessing.compare_exchange_strong(expected, true)) {
            // 当前 item 正在被其他线程并发处理中，我们本次不予重复执行
            continue;
        }

        // 只有当 I/O 请求被完全取消（ERROR_OPERATION_ABORTED）或者 WatchItem 本身不在活跃列表中时，
        // 需将其从 outstanding 集合中安全移除，避免内存泄露。
        DWORD err = ok ? 0 : GetLastError();
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_watches.find(activeItem->path);
            if (it != m_watches.end() && it->second == activeItem) {
                active = true;
            }
        }

        if (err == ERROR_OPERATION_ABORTED || !active) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_outstandingWatches.erase(activeItem);
            activeItem->isProcessing = false;
            continue;
        }

        handleNotification(activeItem, bytesTransferred);

        // 重设 processing 状态
        activeItem->isProcessing = false;

        // 解析完成后，若该 item 依然活跃，则重新挂起 ReadDirectoryChangesW
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
        }
    }
}

void NativeFolderWatcher::handleNotification(std::shared_ptr<WatchItem> item, DWORD bytesTransferred) {
    // 拦截 3. 缓冲区溢出时的自愈对账（解决缺陷 3）
    if (bytesTransferred == 0) {
        qWarning() << "[Watcher] 检测到监控缓冲区溢出（变更信号极其密集），启动全量级联扫描自愈对账...";
        std::wstring folderPath = item->path;
        QMetaObject::invokeMethod(&MetadataManager::instance(), [folderPath]() {
            (void)QtConcurrent::run([folderPath]() {
                AutoImportManager::instance().handleRecursiveIngestion(folderPath);
            });
        }, Qt::QueuedConnection);
        return;
    }

    BYTE* pBase = item->buffer;
    QString lastOldPath;

    while (true) {
        FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)pBase;
        std::wstring fileName(notify->FileName, notify->FileNameLength / sizeof(WCHAR));
        
        // 统一使用 Windows 原生分隔符拼接路径，并确保格式标准化
        QString qFullPath = QString::fromStdWString(item->path);
        qFullPath.append("/");
        qFullPath.append(QString::fromStdWString(fileName));
        qFullPath = QDir::toNativeSeparators(qFullPath);

        std::wstring fullPath = qFullPath.toStdWString();

        qDebug() << "[Watcher] IOCP 收到原始信号 Action:" << notify->Action << "Path:" << qFullPath;

        // 过滤规则：严禁监控 .arcmeta 目录自身的变动，防止死循环
        if (qFullPath.contains("/.arcmeta") || qFullPath.contains("\\.arcmeta")) {
            qDebug() << "[Watcher] 过滤内部数据库变动信号:" << qFullPath;
            if (notify->NextEntryOffset == 0) break;
            pBase += notify->NextEntryOffset;
            continue;
        }

        // 智能重命名事件合并与元数据继承（解决缺陷 2）
        if (notify->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            lastOldPath = qFullPath;
            
            // 启动 50ms 跨缓冲区延时重构防护，处理可能跨通知的情况
            QMetaObject::invokeMethod(this, [this, qFullPath]() {
                handleOldName(qFullPath);
            }, Qt::QueuedConnection);

        } else if (notify->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            if (!lastOldPath.isEmpty()) {
                // 如果在同一个解析链表流中，上一个正好是 OLD_NAME
                QString oldPath = lastOldPath;
                QString newPath = qFullPath;
                lastOldPath.clear();

                // 立即取消对应的延时清除，执行无损迁移事务
                QMetaObject::invokeMethod(this, [this, oldPath, newPath]() {
                    if (m_pendingRenameOldPath == oldPath) {
                        m_pendingRenameOldPath.clear();
                    }
                    QMetaObject::invokeMethod(&MetadataManager::instance(), [oldPath, newPath]() {
                        MetadataManager::instance().syncAfterMove(oldPath.toStdWString(), newPath.toStdWString());
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            } else {
                // 跨缓冲区匹配
                QMetaObject::invokeMethod(this, [this, qFullPath]() {
                    handleNewName(qFullPath);
                }, Qt::QueuedConnection);
            }

        } else if (notify->Action == FILE_ACTION_ADDED || notify->Action == FILE_ACTION_MODIFIED) {
            // 事件防抖与去重引擎（解决缺陷 5）
            QMetaObject::invokeMethod(this, [this, qFullPath]() {
                enqueueAddOrModify(qFullPath);
            }, Qt::QueuedConnection);

        } else if (notify->Action == FILE_ACTION_REMOVED) {
            qDebug() << "[Watcher] 检测到物理删除事件，立即执行数据库物理清洗";
            std::wstring pathStr = fullPath;
            if (pathStr.find(L"ArcMeta.Library_") != std::wstring::npos) {
                emit managedFolderRemoved(pathStr);
            }
            QMetaObject::invokeMethod(&MetadataManager::instance(), [fullPath]() {
                qDebug() << "[Watcher] 异步回调执行: 开始彻底物理清退流程" << QString::fromStdWString(fullPath);
                MetadataManager::instance().removeMetadataSync(fullPath);
            }, Qt::QueuedConnection);
        } else {
            qDebug() << "[Watcher] 非目标 Action (" << notify->Action << ")，跳过处理";
        }

        if (notify->NextEntryOffset == 0) break;
        pBase += notify->NextEntryOffset;
    }
}

void NativeFolderWatcher::enqueueAddOrModify(const QString& path) {
    m_debounceAddQueue.insert(path);
    if (!m_debounceTimer->isActive()) {
        m_debounceTimer->start();
    }
}

void NativeFolderWatcher::processDebounceQueue() {
    if (m_debounceAddQueue.isEmpty()) return;

    QStringList filePaths;
    QStringList folderPaths;

    for (const QString& path : m_debounceAddQueue) {
        QFileInfo info(path);
        if (info.isDir()) {
            folderPaths.append(path);
        } else {
            filePaths.append(path);
        }
    }
    m_debounceAddQueue.clear();

    // 目录级变动分发：触发级联扫描与重构
    for (const QString& folderPath : folderPaths) {
        std::wstring fullPath = folderPath.toStdWString();
        (void)QtConcurrent::run([fullPath]() {
            AutoImportManager::instance().handleRecursiveIngestion(fullPath);
        });
    }

    // 文件级变动分发：批量接口进行去重登记解析
    if (!filePaths.isEmpty()) {
        MetadataManager::instance().registerItemsAsync(filePaths, true);
    }
}

void NativeFolderWatcher::handleOldName(const QString& oldPath) {
    m_pendingRenameOldPath = oldPath;

    // 延迟 50ms。如果在 50ms 内没有收到与之配对的 NEW_NAME，则认为该文件已经被完全物理清退
    QTimer::singleShot(50, this, [this, oldPath]() {
        if (m_pendingRenameOldPath == oldPath) {
            m_pendingRenameOldPath.clear();
            std::wstring fullPath = oldPath.toStdWString();

            qDebug() << "[Watcher] 智能重命名延迟超时，执行完全物理清退" << oldPath;
            if (fullPath.find(L"ArcMeta.Library_") != std::wstring::npos) {
                emit managedFolderRemoved(fullPath);
            }
            QMetaObject::invokeMethod(&MetadataManager::instance(), [fullPath]() {
                MetadataManager::instance().removeMetadataSync(fullPath);
            }, Qt::QueuedConnection);
        }
    });
}

void NativeFolderWatcher::handleNewName(const QString& newPath) {
    if (!m_pendingRenameOldPath.isEmpty()) {
        QString oldPath = m_pendingRenameOldPath;
        m_pendingRenameOldPath.clear();

        qDebug() << "[Watcher] 跨缓冲区匹配成功，旧路径:" << oldPath << "新路径:" << newPath;
        QMetaObject::invokeMethod(&MetadataManager::instance(), [oldPath, newPath]() {
            MetadataManager::instance().syncAfterMove(oldPath.toStdWString(), newPath.toStdWString());
        }, Qt::QueuedConnection);
    } else {
        // 无孤立 OLD_NAME，作为普通 ADD 处理
        enqueueAddOrModify(newPath);
    }
}

} // namespace ArcMeta
