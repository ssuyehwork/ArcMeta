#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

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
    
    // 全局订阅，由内部 logic 过滤
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
}

AutoImportManager::~AutoImportManager() {
}

void AutoImportManager::setDriveListening(const QString& drive, bool active) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (active) {
        m_activeDrives.insert(drive.toUpper());
    } else {
        m_activeDrives.remove(drive.toUpper());
    }
    qDebug() << "[AutoImport] 盘符监听状态变更:" << drive << "->" << active;
}

void AutoImportManager::setDrivePaused(const QString& drive, bool paused) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_drivePausedMap[drive.toUpper()] = paused;
    if (!paused) {
        // 恢复时立即触发一次队列检查
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

bool AutoImportManager::isDrivePaused(const QString& drive) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_drivePausedMap.value(drive.toUpper(), false);
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString drive;
    std::wstring path = MftReader::instance().getFullPath(idx).toStdWString();
    if (isPathInManagedLibrary(path, drive)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingPaths.push_back(path);
        }
        emit tasksStarted(drive);
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    // 更新行为与新增一致
    onEntryAdded(key);
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    // 物理删除触发出库
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString drive;
    std::wstring path = MftReader::instance().getFullPath(idx).toStdWString();
    if (isPathInManagedLibrary(path, drive)) {
        // 判定是否已入库 (通过 hasUserOperations 判定受控状态)
        RuntimeMeta rm = MetadataManager::instance().getMeta(path);
        if (rm.hasUserOperations()) {
            qDebug() << "[AutoImport] 托管文件被物理删除，触发出库:" << QString::fromStdWString(path);
            MetadataManager::instance().setInvalid(path, true);
        }
    }
}

bool AutoImportManager::isPathInManagedLibrary(const std::wstring& path, QString& outDrive) {
    if (path.length() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    
    QString driveStr = QString::fromWCharArray(&path[0], 2).toUpper();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_activeDrives.contains(driveStr)) return false;
    }

    // 精准过滤: [Drive]:\ArcMeta.Library\
    QString targetPath = QString::fromStdWString(path);
    QString libraryPrefixStr = driveStr + "\\ArcMeta.Library\\";
    
    if (targetPath.startsWith(libraryPrefixStr, Qt::CaseInsensitive)) {
        outDrive = driveStr;
        return true;
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
            // 还有挂起的任务，继续等待
            QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
        }
        return;
    }

    // 按盘符聚合
    std::map<QString, std::vector<std::wstring>> pathsByDrive;
    for (const auto& p : pathsToProcess) {
        pathsByDrive[QString::fromWCharArray(&p[0], 2).toUpper()].push_back(p);
    }

    for (auto& pair : pathsByDrive) {
        const QString& drive = pair.first;
        std::wstring vol = MetadataManager::getVolumeSerialNumber(drive.toStdWString() + L"\\");
        
        // 挂载并入库
        DatabaseManager::instance().getMemoryDb(vol, drive.left(1));
        for (const auto& path : pair.second) {
            MetadataManager::instance().registerItem(path);
        }
        
        emit tasksCompleted(drive);
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
