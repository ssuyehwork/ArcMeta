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
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef run
#undef run
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
        // 2026-10-29 缓存路径前缀，确保判定逻辑高效且符合最新命名规范
        QString prefix = getManagedLibraryPath(d);
        if (!prefix.endsWith("\\")) prefix += "\\";
        m_activeDrivePrefixes[d] = prefix;

        Logger::log(QString("[AutoImport] 盘符监听已【开启】: %1 (托管区: %2)").arg(d, prefix));
    } else {
        m_activeDrives.remove(d);
        m_activeDrivePrefixes.remove(d);
        Logger::log(QString("[AutoImport] 盘符监听已【关闭】: %1").arg(d));
    }
}

QString AutoImportManager::getManagedLibraryPath(const QString& driveLetter) {
    if (driveLetter.isEmpty()) return "";
    
    QString cleanLetter = driveLetter.at(0).toUpper();
    QString root = cleanLetter + ":\\";
    
    // 获取卷序列号 (如 4DFFAF5E)
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(root.toStdWString());
    QString serialStr = QString::fromStdWString(volSerial).toUpper();

    // 格式：Arcmeta_4DFFAF5E_I (注意：I 是盘符)
    return QString("%1Arcmeta_%2_%3").arg(root).arg(serialStr).arg(cleanLetter);
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
    // 2026-10-29 按照 Plan-104：统一标识符为 targetPath
    QString targetPath = QString::fromStdWString(path);

    // 工业级排查：记录所有被捕获到的变更，确定 MFT 引擎是否断路
    // Logger::log(QString("[AutoImport] USN 捕获原始路径: %1").arg(targetPath));

    if (isPathInManagedLibrary(targetPath, drive)) {
        Logger::log(QString("[AutoImport] >>> 判定通过：属于托管库路径: %1").arg(targetPath));
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
    // 2026-10-29 按照 Plan-104：统一标识符为 targetPath
    QString targetPath = QString::fromStdWString(path);

    if (isPathInManagedLibrary(targetPath, drive)) {
        Logger::log(QString("[AutoImport] 捕获到移除项: %1").arg(targetPath));
        MetadataManager::instance().setInvalid(path, true);
    }
}

bool AutoImportManager::isPathInManagedLibrary(const QString& targetPath, QString& outDrive) {
    // 2026-10-29 按照用户要求：适配 Arcmeta_SERIAL_LETTER 命名规范
    if (targetPath.length() < 3 || targetPath[1] != ':' || targetPath[2] != '\\') return false;
    
    QString dStr = targetPath.left(2).toUpper();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeDrivePrefixes.contains(dStr)) {
        return false;
    }

    const QString& managedLibraryPrefix = m_activeDrivePrefixes[dStr];
    
    if (targetPath.startsWith(managedLibraryPrefix, Qt::CaseInsensitive)) {
        outDrive = dStr;
        return true;
    }

    // 协助定位路径标准化问题 (带序列号的新版)
    if (targetPath.contains("Arcmeta_", Qt::CaseInsensitive)) {
         Logger::log(QString("[AutoImport] 判定拦截：路径包含关键字但前缀匹配失败. 预期前缀: %1, 实际路径: %2")
                    .arg(managedLibraryPrefix, targetPath));
    }
    
    return false;
}

void AutoImportManager::scanManagedLibrary(const QString& drive) {
    QString d = drive.toUpper();
    // 2026-10-29 按照用户要求：存量扫描逻辑 (适配新命名规范)
    QString targetPrefix = getManagedLibraryPath(d);
    
    Logger::log(QString("[AutoImport] 开始存量扫描托管库: %1").arg(targetPrefix));

    (void)QtConcurrent::run([this, d, targetPrefix]() {
        QStringList pathsToImport;
        
        // 基于 MFT 内存索引遍历 (秒级完成)
        int count = MftReader::instance().totalCount();
        for (int i = 0; i < count; ++i) {
            QString fullPath = MftReader::instance().getFullPath(i);
            if (fullPath.startsWith(targetPrefix, Qt::CaseInsensitive)) {
                // 仅入库文件，文件夹由 ImportHelper 递归处理或单独登记
                QFileInfo fi(fullPath);
                if (fi.isFile()) {
                    pathsToImport << fullPath;
                }
            }
        }

        if (!pathsToImport.isEmpty()) {
            Logger::log(QString("[AutoImport] 存量扫描完成，发现 %1 个待入库文件").arg(pathsToImport.size()));
            emit tasksStarted(d);
            
            // 调用归一化入库接口
            QFuture<void> future = ImportHelper::importPaths(pathsToImport, 0, nullptr, false);
            
            QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
            connect(watcher, &QFutureWatcher<void>::finished, this, [this, d, watcher]() {
                Logger::log(QString("[AutoImport] 存量入库完成，盘符: %1").arg(d));
                emit tasksCompleted(d);
                watcher->deleteLater();
                MetadataManager::instance().notifyFullUIRebuild();
            });
            watcher->setFuture(future);
        } else {
            Logger::log(QString("[AutoImport] 托管库 %1 下未发现存量文件").arg(targetPrefix));
        }
    });
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
