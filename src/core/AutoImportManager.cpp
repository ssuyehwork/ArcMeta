#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../util/ImportHelper.h"
#include "../util/ShellHelper.h"
#include "AppConfig.h"
#include <QDebug>
#include <string>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <functional>
#include <QtConcurrent>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
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
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entriesBatchAdded, this, &AutoImportManager::onEntriesBatchAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entriesBatchAdded, this, &AutoImportManager::onEntriesBatchAdded);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);
    
    QString driveLetter = MetadataManager::instance().getDriveLetterByMftIndex(driveIdx);
    if (driveLetter.isEmpty()) return;

    HANDLE hVol = CreateFileW((L"\\\\.\\" + driveLetter.toStdWString()).c_str(), 
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol != INVALID_HANDLE_VALUE) {
        QString relPath = MftReader::getPathByFrn(hVol, frn);
        CloseHandle(hVol);
        if (!relPath.isEmpty()) {
            QString fullPath = driveLetter + relPath;
            std::wstring wPath = fullPath.toStdWString();

            if (isPathInManagedLibrary(wPath)) {
                MetadataManager::instance().registerItem(wPath);
                
                if (QFileInfo(fullPath).isDir()) {
                    (void)QtConcurrent::run([fullPath]() {
                        QStringList toRegister;
                        QDirIterator it(fullPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            toRegister << it.next();
                            if (toRegister.size() >= 100) {
                                MetadataManager::instance().registerItemsAsync(toRegister);
                                toRegister.clear();
                            }
                        }
                        if (!toRegister.isEmpty()) MetadataManager::instance().registerItemsAsync(toRegister);
                    });
                }

                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_pendingPaths.push_back(wPath);
                }
                QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
            }
        }
    }
}

void AutoImportManager::onEntriesBatchAdded(int driveIdx, const QList<uint64_t>& frns) {
    QString driveLetter = MetadataManager::instance().getDriveLetterByMftIndex(driveIdx);
    if (driveLetter.isEmpty()) return;

    HANDLE hVol = CreateFileW((L"\\\\.\\" + driveLetter.toStdWString()).c_str(), 
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> toAdd;
    for (uint64_t frn : frns) {
        QString relPath = MftReader::getPathByFrn(hVol, frn);
        if (!relPath.isEmpty()) {
            QString fullPath = driveLetter + relPath;
            std::wstring wPath = fullPath.toStdWString();
            if (isPathInManagedLibrary(wPath)) {
                MetadataManager::instance().registerItem(wPath);
                toAdd.push_back(wPath);

                if (QFileInfo(fullPath).isDir()) {
                    (void)QtConcurrent::run([fullPath]() {
                        QStringList toRegister;
                        QDirIterator it(fullPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            toRegister << it.next();
                            if (toRegister.size() >= 100) {
                                MetadataManager::instance().registerItemsAsync(toRegister);
                                toRegister.clear();
                            }
                        }
                        if (!toRegister.isEmpty()) MetadataManager::instance().registerItemsAsync(toRegister);
                    });
                }
            }
        }
    }
    CloseHandle(hVol);

    if (!toAdd.empty()) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (const auto& p : toAdd) m_pendingPaths.push_back(p);
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);
    
    QString driveLetter = MetadataManager::instance().getDriveLetterByMftIndex(driveIdx);
    if (driveLetter.isEmpty()) return;

    HANDLE hVol = CreateFileW((L"\\\\.\\" + driveLetter.toStdWString()).c_str(), 
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol != INVALID_HANDLE_VALUE) {
        QString relPath = MftReader::getPathByFrn(hVol, frn);
        CloseHandle(hVol);
        if (!relPath.isEmpty()) {
            QString fullPath = driveLetter + relPath;
            std::wstring wPath = fullPath.toStdWString();

            if (isPathInManagedLibrary(wPath)) {
                MetadataManager::instance().registerItem(wPath);
                
                if (QFileInfo(fullPath).isDir()) {
                    (void)QtConcurrent::run([fullPath]() {
                        QStringList toRegister;
                        QDirIterator it(fullPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            toRegister << it.next();
                            if (toRegister.size() >= 100) {
                                MetadataManager::instance().registerItemsAsync(toRegister);
                                toRegister.clear();
                            }
                        }
                        if (!toRegister.isEmpty()) MetadataManager::instance().registerItemsAsync(toRegister);
                    });
                }

                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_pendingPaths.push_back(wPath);
                QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
            }

            // 2026-11-xx 按照 Plan-113：物理重命名同步 (物理 -> 逻辑)
            // 当 USN 监听到库内物理文件夹重命名成功后，同步更新逻辑分类名
            if (QFileInfo(fullPath).isDir()) {
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wPath);
                std::string fid = MetadataManager::generateFallbackFid(volSerial, std::to_wstring(frn));
                std::wstring oldPath = MetadataManager::instance().getPathByFid(fid);
                
                if (!oldPath.empty() && oldPath != wPath) {
                    QString oldName = QFileInfo(QString::fromStdWString(oldPath)).fileName();
                    QString newName = QFileInfo(fullPath).fileName();
                    
                    if (oldName != newName) {
                        qDebug() << "[AutoImport] 物理重命名同步 (IO 确认):" << oldName << "->" << newName;
                        MetadataManager::instance().renameItem(oldPath, wPath);
                    }
                }
            }
        }
    }
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);

    // 2026-11-xx 按照 Plan-113：修正盘符还原逻辑
    QString driveLetter = MetadataManager::instance().getDriveLetterByMftIndex(driveIdx);
    if (driveLetter.isEmpty()) return;

    // 2026-11-xx 按照 Plan-113：标记为失效
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber((driveLetter + "\\").toStdWString());
    std::string fidPrefix = QString::fromStdWString(volSerial).toStdString() + "_" + std::to_string(frn);
    MetadataManager::instance().setInvalidByFidPrefix(fidPrefix, true);
    
    // 同步更新 ingestionStatus 为 Invalid (-1)
    std::wstring p = MetadataManager::instance().getPathByFid(MetadataManager::generateFallbackFid(volSerial, std::to_wstring(frn)));
    if (!p.empty()) {
        MetadataManager::instance().setIngestionStatus(p, -1);
    }
}

void AutoImportManager::startTask(const QString& drive) {
    m_globalPaused.store(false);

    (void)QtConcurrent::run([this, drive]() {
        // 按照 Plan-113：扫描库内所有 Registered (0) 的项并发起解析
        QString managedPath = QString::fromStdWString(getManagedLibraryPath(drive.toStdWString()));
        if (managedPath.isEmpty()) return;

        QStringList toProcess;
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            if (meta.ingestionStatus == 0 && QString::fromStdWString(path).startsWith(managedPath, Qt::CaseInsensitive)) {
                toProcess << QString::fromStdWString(path);
            }
        });

        if (!toProcess.isEmpty()) {
            ImportHelper::importPaths(toProcess, 0, nullptr);
            // importPaths 内部完成 registerItem 会将 ingestionStatus 设置为 Ingested(1) 吗？
            // 我们需要在 registerItem 逻辑中补全。
        }

        emit taskFinished(drive);
    });
}

void AutoImportManager::pauseTask(const QString& drive) {
    Q_UNUSED(drive);
    m_globalPaused.store(true);
}

bool AutoImportManager::isPathInManagedLibrary(const std::wstring& path) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return false;
    std::wstring managed = instance().getManagedFolderAbsolutePath(volSerial);
    if (managed.empty()) return false;

    QString nPath = QDir::toNativeSeparators(QString::fromStdWString(path)).toLower();
    QString nManaged = QDir::toNativeSeparators(QString::fromStdWString(managed)).toLower();
    
    // 确保 nManaged 末尾有反斜杠后再比较，防止前缀误匹配
    if (!nManaged.endsWith('\\')) nManaged += '\\';
    return nPath.startsWith(nManaged);
}

std::wstring AutoImportManager::getManagedLibraryPath(const std::wstring& pathInDrive) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(pathInDrive);
    return instance().getManagedFolderAbsolutePath(volSerial);
}

void AutoImportManager::ensureManagedFolderExists(const std::wstring& driveRoot) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(driveRoot);
    if (volSerial.empty()) return;
    
    std::wstring managed = instance().getManagedFolderAbsolutePath(volSerial);
    if (managed.empty()) {
        // 创建默认的
        QString drive = QString::fromStdWString(driveRoot);
        if (drive.length() == 2 && drive[1] == ':') drive += "/";
        QString defaultManaged = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
        QDir().mkpath(defaultManaged);
    }
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
    
    // 2026-11-15 按照 Plan-108：若配置不存在，尝试使用默认托管路径 ArcMeta.Library_[DriveLetter]
    if (relPath.isEmpty()) {
        QString letter = drive.left(1);
        relPath = "ArcMeta.Library_" + letter;
        if (!QDir(drive + relPath).exists()) return L"";
    }

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

        // 提取其中一个路径的盘符用于重命名纠偏
        QString letter = "";
        if (!pair.second.empty()) {
            const std::wstring& firstPath = pair.second.front();
            if (firstPath.length() >= 2 && firstPath[1] == L':') {
                letter = QString::fromWCharArray(&firstPath[0], 1);
            }
        }

        // 静默强制挂载数据库
        DatabaseManager::instance().getMemoryDb(vol, letter);

        for (const auto& path : pair.second) {
            MetadataManager::instance().registerItem(path);
            // 2026-11-xx 按照 Plan-3：由 setItemVisualMetadata 解析成功后流转状态，此处禁止手动设为 1
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
