#include "AutoImportManager.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../meta/CategoryRepo.h"
#include "AppConfig.h"
#include "HistoryPathManager.h"
#include "CategoryStructureMapper.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <functional>
#include <cwchar>
#include <map>
#include <cstdint>
#include <atomic>

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
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    m_isListening = true;
    qDebug() << "[AutoImport] USN 监听已停用，不连接 MftReader 信号";
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    m_isListening = false;
    qDebug() << "[AutoImport] USN 监听已停止";
}

void AutoImportManager::syncAllManagedLibraries() {
    const auto drives = QDir::drives();
    bool changed = false;
    for (const QFileInfo& d : drives) {
        QString drive = d.absolutePath();
        QString letter = drive.left(1).toUpper();
        
        QDir rootDir(drive);
        QStringList entries = rootDir.entryList({"ArcMeta.Library_*"}, QDir::Dirs | QDir::Hidden);
        
        QString targetName = "ArcMeta.Library_" + letter;
        for (const QString& entry : entries) {
            if (QString::compare(entry, targetName, Qt::CaseInsensitive) == 0) {
                QString managedPath = rootDir.absoluteFilePath(entry);
                qDebug() << "[AutoImport] 启动对账：发现物理托管库，执行同步 ->" << managedPath;
                (void)QtConcurrent::run([this, managedPath]() {
                    handleRecursiveIngestion(QDir::toNativeSeparators(managedPath).toStdWString());
                });
                changed = true;
            }
        }
    }
    if (changed) {
        MetadataManager::instance().notifyFullUIRebuild();
    }
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    Q_UNUSED(key);
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    Q_UNUSED(key);
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    Q_UNUSED(key);
}

void AutoImportManager::recordRecentVisitedFolder(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring managedFolder;
    if (instance().checkAndGetManagedPath(path, managedFolder)) return;

    HistoryPathManager::recordRecentVisitedFolder(path);
}

QStringList AutoImportManager::getRecentVisitedFolders(const std::wstring& volSerial) {
    return HistoryPathManager::getRecentVisitedFolders(volSerial);
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring managedAbs = getManagedLibraryPath(path);
    if (managedAbs.empty()) return false;

    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

std::wstring AutoImportManager::getManagedLibraryPath(const std::wstring& pathOrVolSerial) {
    if (pathOrVolSerial.empty()) return L"";

    std::wstring volSerial = pathOrVolSerial;
    if (volSerial.find(L":") != std::wstring::npos || volSerial.find(L"\\") != std::wstring::npos) {
        volSerial = MetadataManager::getVolumeSerialNumber(pathOrVolSerial);
    }
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

    QString drive;
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        if (MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString()) == volSerial) {
            drive = d.absolutePath();
            break;
        }
    }
    if (drive.isEmpty()) return L"";

    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();

    if (relPath.isEmpty()) {
        relPath = "ArcMeta.Library_" + drive.left(1).toUpper();
        bool exists = QDir(drive + relPath).exists(); 
        if (!exists) return L"";
    }

    std::wstring result = MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
    return result;
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    (void)QtConcurrent::run([this, pathsToProcess]() {
        MetadataManager::instance().setInternalOperating(true);

        std::map<std::wstring, std::vector<std::wstring>> pathsByVol;
        for (const auto& p : pathsToProcess) {
            pathsByVol[MetadataManager::getVolumeSerialNumber(p)].push_back(p);
        }

        for (auto& pair : pathsByVol) {
            const std::wstring& vol = pair.first;
            if (vol.empty()) continue;

            auto driveLock = DatabaseManager::instance().getDriveMutex(vol);
            std::lock_guard<std::mutex> dLock(*driveLock);

            QString letter = "";
            if (!pair.second.empty()) {
                const std::wstring& firstPath = pair.second.front();
                if (firstPath.length() >= 2 && firstPath[1] == L':') {
                    letter = QString::fromWCharArray(&firstPath[0], 1);
                }
            }

            DatabaseManager::instance().getMemoryDb(vol, letter);

            for (const auto& path : pair.second) {
                MetadataManager::instance().registerItem(path, true);
            }
        }

        MetadataManager::instance().setInternalOperating(false);
        MetadataManager::instance().notifyFullUIRebuild();
    });
}

bool AutoImportManager::isUnderManagedLibrary(uint64_t key) {
    Q_UNUSED(key);
    return false;
}

void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    // 先全局锁，后分库锁
    std::lock_guard<std::mutex> globalLock(DatabaseManager::instance().getGlobalMutex());
    
    std::wstring vol = MetadataManager::getVolumeSerialNumber(rootPath);
    auto driveLock = DatabaseManager::instance().getDriveMutex(vol);
    std::lock_guard<std::mutex> dLock(*driveLock);

    MetadataManager::instance().setInternalOperating(true);
    
    int rootCatId = 0;
    CategoryStructureMapper::ensureCategoryPath(rootPath, rootCatId);

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();
}

} // namespace ArcMeta
