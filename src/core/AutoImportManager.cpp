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
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;

    if (checkAndGetManagedPath(fullPath, managedFolder)) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);

        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return false;

    std::wstring managedAbs = getManagedFolderAbsolutePath(volSerial);
    if (managedAbs.empty()) return false;

    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

std::wstring AutoImportManager::getManagedFolderAbsolutePath(const std::wstring& volSerial) {
    // 根据序列号反查当前盘符 (Plan-68 4.1)
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
    if (relPath.isEmpty()) return L"";

    return MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    // 通道 3：按照磁盘聚合并执行静默挂载与入库
    std::map<std::wstring, std::vector<std::wstring>> pathsByVol;
    for (const auto& p : pathsToProcess) {
        pathsByVol[MetadataManager::getVolumeSerialNumber(p)].push_back(p);
    }

    for (auto& pair : pathsByVol) {
        const std::wstring& vol = pair.first;
        if (vol.empty()) continue;

        // 静默强制挂载数据库
        DatabaseManager::instance().getMemoryDb(vol);

        for (const auto& path : pair.second) {
            MetadataManager::instance().registerItem(path);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
