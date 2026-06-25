#include "AutoImportManager.h"
#include <QString>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../util/ImportHelper.h"
#include "../ui/Logger.h"
#include "AppConfig.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

AutoImportManager& AutoImportManager::instance() {
    static AutoImportManager inst;
    return inst;
}

AutoImportManager::AutoImportManager(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setInterval(3000); 
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &AutoImportManager::processImportQueue);
    
    // 全局订阅 USN 变更
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
}

AutoImportManager::~AutoImportManager() {
}

void AutoImportManager::setDriveListening(const QString& drive, bool active) {
    std::lock_guard<std::mutex> lock(m_mutex);
    QString d = drive.toUpper();
    if (active) {
        m_activeDrives.insert(d);
        Logger::log(QString("[AutoImport] 盘符监听已【开启】: %1").arg(d));
    } else {
        m_activeDrives.remove(d);
        Logger::log(QString("[AutoImport] 盘符监听已【关闭】: %1").arg(d));
    }
}

void AutoImportManager::setDrivePaused(const QString& drive, bool paused) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_drivePausedMap[drive.toUpper()] = paused;
    if (!paused) {
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

bool AutoImportManager::isDrivePaused(const QString& drive) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_drivePausedMap.value(drive.toUpper(), false);
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) {
        // Logger::log(QString("[AutoImport] 收到无效 Entry Key: %1").arg(key));
        return;
    }

    QString drive;
    std::wstring path = MftReader::instance().getFullPath(idx).toStdWString();
    QString qPath = QString::fromStdWString(path);

    // 工业级排查：记录所有被捕获到的变更，确定 MFT 引擎是否断路
    // Logger::log(QString("[AutoImport] USN 捕获原始路径: %1").arg(qPath));

    if (isPathInManagedLibrary(path, drive)) {
        Logger::log(QString("[AutoImport] >>> 判定通过：属于托管库路径: %1").arg(qPath));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingPaths.push_back(path);
        }
        emit tasksStarted(drive);
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    onEntryAdded(key);
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString drive;
    std::wstring path = MftReader::instance().getFullPath(idx).toStdWString();
    if (isPathInManagedLibrary(path, drive)) {
        Logger::log(QString("[AutoImport] 捕获到移除项: %1").arg(QString::fromStdWString(path)));
        MetadataManager::instance().setInvalid(path, true);
    }
}

bool AutoImportManager::isPathInManagedLibrary(const std::wstring& path, QString& outDrive) {
    if (path.length() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    
    QString dStr = QString::fromWCharArray(&path[0], 2).toUpper();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_activeDrives.contains(dStr)) {
            // Logger::log(QString("[AutoImport] 判定拦截：盘符 %1 未处于激活状态. 路径: %2").arg(dStr, QString::fromStdWString(path)));
            return false;
        }
    }

    const QString pStr = QString::fromStdWString(path);
    const QString libPrefix = dStr + "\\ArcMeta.Library\\";
    
    if (pStr.startsWith(libPrefix, Qt::CaseInsensitive)) {
        outDrive = dStr;
        return true;
    }

    // 仅在路径确实包含 ArcMeta.Library 关键字但前缀匹配失败时记录，协助定位路径标准化问题
    if (pStr.contains("ArcMeta.Library", Qt::CaseInsensitive)) {
         Logger::log(QString("[AutoImport] 判定拦截：路径包含关键字但前缀匹配失败. 预期前缀: %1, 实际路径: %2").arg(libPrefix, pStr));
    }

    return false;
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    std::vector<std::wstring> stillPending;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& p : m_pendingPaths) {
            if (p.length() < 2) continue;
            QString drive = QString::fromWCharArray(&p[0], 2).toUpper();
            bool isPaused = m_drivePausedMap.value(drive, false);
            if (isPaused) {
                stillPending.push_back(p);
            } else {
                pathsToProcess.push_back(p);
            }
        }
        m_pendingPaths = std::move(stillPending);
    }

    if (pathsToProcess.empty()) {
        if (!m_pendingPaths.empty()) {
            QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
        }
        return;
    }

    std::map<QString, std::vector<std::wstring>> pathsByDrive;
    for (const auto& p : pathsToProcess) {
        if (p.length() < 2) continue;
        pathsByDrive[QString::fromWCharArray(&p[0], 2).toUpper()].push_back(p);
    }

    for (auto& pair : pathsByDrive) {
        const QString& drive = pair.first;
        QStringList paths;
        for (const auto& p : pair.second) {
            paths << QString::fromStdWString(p);
        }

        if (!paths.isEmpty()) {
            Logger::log(QString("[AutoImport] 启动批量入库，盘符: %1, 数量: %2").arg(drive).arg(paths.size()));
            QFuture<void> future = ImportHelper::importPaths(paths, 0, nullptr, false);

            // 2026-07-21 按照 Plan-102：使用 Watcher 追踪任务完成状态，确保 UI 状态同步
            QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
            connect(watcher, &QFutureWatcher<void>::finished, this, [this, drive, watcher]() {
                Logger::log(QString("[AutoImport] 批量入库完成，盘符: %1").arg(drive));
                emit tasksCompleted(drive);
                watcher->deleteLater();
            });
            watcher->setFuture(future);
        } else {
            emit tasksCompleted(drive);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
}

} // namespace ArcMeta
