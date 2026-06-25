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
    if (active) {
        m_activeDrives.insert(drive.toUpper());
    } else {
        m_activeDrives.remove(drive.toUpper());
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
    onEntryAdded(key);
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString drive;
    std::wstring path = MftReader::instance().getFullPath(idx).toStdWString();
    if (isPathInManagedLibrary(path, drive)) {
        RuntimeMeta rm = MetadataManager::instance().getMeta(path);
        if (rm.hasUserOperations()) {
            MetadataManager::instance().setInvalid(path, true);
        }
    }
}

bool AutoImportManager::isPathInManagedLibrary(const std::wstring& path, QString& outDrive) {
    if (path.length() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    
    QString dStr = QString::fromWCharArray(&path[0], 2).toUpper();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_activeDrives.contains(dStr)) return false;
    }

    const QString pStr = QString::fromStdWString(path);
    const QString libPrefix = dStr + "\\ArcMeta.Library\\";
    
    if (pStr.startsWith(libPrefix, Qt::CaseInsensitive)) {
        outDrive = dStr;
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
        std::wstring vol = MetadataManager::getVolumeSerialNumber(drive.toStdWString() + L"\\");
        DatabaseManager::instance().getMemoryDb(vol, drive.left(1));
        for (const auto& p : pair.second) {
            MetadataManager::instance().registerItem(p);
        }
        emit tasksCompleted(drive);
    }

    MetadataManager::instance().notifyFullUIRebuild();
}

} // namespace ArcMeta
