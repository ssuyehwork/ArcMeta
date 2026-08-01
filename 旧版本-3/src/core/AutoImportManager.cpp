#include "AutoImportManager.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../meta/CategoryRepo.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QCryptographicHash>
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
}

void AutoImportManager::stopListening() {
}

void AutoImportManager::syncAllManagedLibraries(bool allowLightweight) {
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
                (void)QtConcurrent::run([this, managedPath, allowLightweight]() {
                    handleRecursiveIngestion(QDir::toNativeSeparators(managedPath).toStdWString(), allowLightweight);
                });
                changed = true;
            }
        }
    }
    if (changed) {
        MetadataManager::instance().notifyFullUIRebuild();
    }
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
            std::lock_guard<std::recursive_mutex> dLock(*driveLock);

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

bool AutoImportManager::hasTopLevelChanged(const std::wstring& rootPath) {
    QFileInfo info(QString::fromStdWString(rootPath));
    if (!info.exists()) return true;

    qint64 currentMtime = info.lastModified().toMSecsSinceEpoch();
    int currentChildCount = QDir(info.absoluteFilePath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();

    QString key = "ScanSnapshot/" + QString::fromUtf8(QCryptographicHash::hash(
        QString::fromStdWString(rootPath).toUtf8(), QCryptographicHash::Md5).toHex());
    QString saved = AppConfig::instance().getValue(key, "").toString();
    QString current = QString("%1:%2").arg(currentMtime).arg(currentChildCount);

    return saved != current;
}

void AutoImportManager::saveTopLevelSnapshot(const std::wstring& rootPath) {
    QFileInfo info(QString::fromStdWString(rootPath));
    if (!info.exists()) return;
    qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    int childCount = QDir(info.absoluteFilePath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
    QString key = "ScanSnapshot/" + QString::fromUtf8(QCryptographicHash::hash(
        QString::fromStdWString(rootPath).toUtf8(), QCryptographicHash::Md5).toHex());
    AppConfig::instance().setValue(key, QString("%1:%2").arg(mtime).arg(childCount));
}

void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath, bool allowLightweight) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    if (allowLightweight && !hasTopLevelChanged(rootPath)) {
        qDebug() << "[AutoImport] [Incremental] 顶层快照无变化，跳过托管库深度递归对账与盘点:" << QString::fromStdWString(rootPath);
        return;
    }

    std::wstring vol = MetadataManager::getVolumeSerialNumber(rootPath);
    auto driveLock = DatabaseManager::instance().getDriveMutex(vol);
    std::lock_guard<std::recursive_mutex> dLock(*driveLock);

    MetadataManager::instance().setInternalOperating(true);

    CategoryRepo::syncPhysicalDirectoryCascade(rootPath);

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();

    saveTopLevelSnapshot(rootPath);
}

} // namespace ArcMeta
