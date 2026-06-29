#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../util/ImportHelper.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#undef run
#endif

namespace ArcMeta {

// ============================================================
// 架构说明（禁止 Jules 修改此注释）：
//
// 入库唯一流程：
//   拖拽/扫描入库  →  Move 文件到 ArcMeta.Library_[盘符]  →  结束
//   USN Journal 感知到 Library 内文件变动
//     →  processPath()  →  registerItem (状态=0, 占坑)
//     →  processImportQueue debounce  →  DB 挂载 + UI 刷新
//     →  ImportHelper 异步处理  →  状态流转为 1 (Ingested)
//
// 断电恢复：
//   startTask()  →  扫描 DB 中 ingestionStatus=0 的记录  →  继续处理
//
// 禁止在任何触发点（拖拽/扫描/USN槽函数）直接调用 registerItem。
// 所有注册必须经过 processPath()。
// 禁止使用 getPathByFrn / CreateFileW 进行路径反查。
// ============================================================

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

// ============================================================
// 信号连接管理
// ============================================================

void AutoImportManager::startListening() {
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded,
            this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryUpdated,
            this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entriesBatchAdded,
            this, &AutoImportManager::onEntriesBatchAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entriesBatchUpdated,
            this, &AutoImportManager::onEntriesBatchUpdated, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved,
            this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    m_isListening = true;
    qDebug() << "[AutoImport] 开始监听 USN 信号";
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded,
               this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated,
               this, &AutoImportManager::onEntryUpdated);
    disconnect(&MftReader::instance(), &MftReader::entriesBatchAdded,
               this, &AutoImportManager::onEntriesBatchAdded);
    disconnect(&MftReader::instance(), &MftReader::entriesBatchUpdated,
               this, &AutoImportManager::onEntriesBatchUpdated);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved,
               this, &AutoImportManager::onEntryRemoved);
    m_isListening = false;
}

// ============================================================
// 核心处理函数（唯一的注册入口）
// ============================================================

void AutoImportManager::processPath(const std::wstring& path) {
    if (path.empty()) return;
    if (!isPathInManagedLibrary(path)) return;

    // 立即占坑：写入 DB，状态=0 (Registered)，断电也不丢
    MetadataManager::instance().registerItem(path);

    // 如果是文件夹，递归占坑其下所有子项
    QString qPath = QString::fromStdWString(path);
    if (QFileInfo(qPath).isDir()) {
        (void)QtConcurrent::run([qPath]() {
            QStringList toRegister;
            QDirIterator it(qPath,
                            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                toRegister << it.next();
                if (toRegister.size() >= 100) {
                    MetadataManager::instance().registerItemsAsync(toRegister);
                    toRegister.clear();
                }
            }
            if (!toRegister.isEmpty())
                MetadataManager::instance().registerItemsAsync(toRegister);
        });
    }

    // 加入 debounce 队列，3 秒后统一挂载 DB + 刷新 UI
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(path);
    }
    QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
}

// ============================================================
// USN 槽函数：只负责从内存索引取路径，然后交给 processPath
// ============================================================

void AutoImportManager::onEntryAdded(uint64_t key) {
    int index = MftReader::instance().getIndexByKey(key);
    if (index < 0) return;
    QString fullPath = MftReader::instance().getFullPath(index);
    if (fullPath.isEmpty()) return;
    processPath(fullPath.toStdWString());
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    int index = MftReader::instance().getIndexByKey(key);
    if (index < 0) return;
    QString fullPath = MftReader::instance().getFullPath(index);
    if (fullPath.isEmpty()) return;

    std::wstring wPath = fullPath.toStdWString();

    // 库内文件夹重命名：同步逻辑分类名
    if (isPathInManagedLibrary(wPath) && QFileInfo(fullPath).isDir()) {
        uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
        std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wPath);
        std::string fid = MetadataManager::generateFallbackFid(volSerial, std::to_wstring(frn));
        std::wstring oldPath = MetadataManager::instance().getPathByFid(fid);
        if (!oldPath.empty() && oldPath != wPath) {
            QString oldName = QFileInfo(QString::fromStdWString(oldPath)).fileName();
            QString newName = QFileInfo(fullPath).fileName();
            if (oldName != newName) {
                qDebug() << "[AutoImport] 物理重命名同步:" << oldName << "->" << newName;
                MetadataManager::instance().renameItem(oldPath, wPath);
            }
        }
    }

    processPath(wPath);
}

void AutoImportManager::onEntriesBatchAdded(int driveIdx, const QList<uint64_t>& frns) {
    for (uint64_t frn : frns) {
        uint64_t key = MftReader::makeKey(static_cast<size_t>(driveIdx), frn);
        int index = MftReader::instance().getIndexByKey(key);
        if (index < 0) continue;
        QString fullPath = MftReader::instance().getFullPath(index);
        if (fullPath.isEmpty()) continue;
        processPath(fullPath.toStdWString());
    }
}

void AutoImportManager::onEntriesBatchUpdated(int driveIdx, const QList<uint64_t>& frns) {
    for (uint64_t frn : frns) {
        uint64_t key = MftReader::makeKey(static_cast<size_t>(driveIdx), frn);
        int index = MftReader::instance().getIndexByKey(key);
        if (index < 0) continue;
        QString fullPath = MftReader::instance().getFullPath(index);
        if (fullPath.isEmpty()) continue;
        processPath(fullPath.toStdWString());
    }
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx  = static_cast<int>(key >> 48);

    QString driveLetter = MetadataManager::instance().getDriveLetterByMftIndex(driveIdx);
    if (driveLetter.isEmpty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(
        (driveLetter + "\\").toStdWString());

    // 标记为物理失效
    std::string fidPrefix = QString::fromStdWString(volSerial).toStdString()
                            + "_" + std::to_string(frn);
    MetadataManager::instance().setInvalidByFidPrefix(fidPrefix, true);

    std::wstring p = MetadataManager::instance().getPathByFid(
        MetadataManager::generateFallbackFid(volSerial, std::to_wstring(frn)));
    if (!p.empty())
        MetadataManager::instance().setIngestionStatus(p, -1);
}

// ============================================================
// startTask：断电恢复，处理 DB 中 ingestionStatus=0 的未完成项
// ============================================================

void AutoImportManager::startTask(const QString& drive) {
    m_globalPaused.store(false);

    (void)QtConcurrent::run([this, drive]() {
        QString managedPath = QString::fromStdWString(
            getManagedLibraryPath(drive.toStdWString()));
        if (managedPath.isEmpty()) {
            emit taskFinished(drive);
            return;
        }

        // 扫描 DB 中所有 Registered(0) 但尚未完成入库的项
        QStringList toProcess;
        MetadataManager::instance().forEachCachedItem(
            [&](const std::wstring& path, const RuntimeMeta& meta) {
                if (meta.ingestionStatus == 0
                    && QString::fromStdWString(path).startsWith(
                           managedPath, Qt::CaseInsensitive)) {
                    toProcess << QString::fromStdWString(path);
                }
            });

        if (!toProcess.isEmpty()) {
            qDebug() << "[AutoImport] 断电恢复：处理未完成项" << toProcess.size() << "个";
            ImportHelper::importPaths(toProcess, 0, nullptr);
        }

        emit taskFinished(drive);
    });
}

void AutoImportManager::pauseTask(const QString& drive) {
    Q_UNUSED(drive);
    m_globalPaused.store(true);
}

// ============================================================
// Debounce 队列：统一挂载 DB + 刷新 UI
// ============================================================

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }
    if (pathsToProcess.empty()) return;

    // 按卷序列号聚合，确保每个卷的 DB 只挂载一次
    std::map<std::wstring, QString> volToLetter;
    for (const auto& p : pathsToProcess) {
        std::wstring vol = MetadataManager::getVolumeSerialNumber(p);
        if (vol.empty() || volToLetter.count(vol)) continue;
        if (p.length() >= 2 && p[1] == L':')
            volToLetter[vol] = QString::fromWCharArray(&p[0], 1);
    }
    for (const auto& pair : volToLetter) {
        DatabaseManager::instance().getMemoryDb(pair.first, pair.second);
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 队列处理完成，项数:" << pathsToProcess.size();
}

// ============================================================
// 路径工具函数
// ============================================================

bool AutoImportManager::isPathInManagedLibrary(const std::wstring& path) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty() || volSerial == L"UNKNOWN") return false;
    std::wstring managed = instance().getManagedFolderAbsolutePath(volSerial);
    if (managed.empty()) return false;

    QString nPath    = QDir::toNativeSeparators(QString::fromStdWString(path)).toLower();
    QString nManaged = QDir::toNativeSeparators(QString::fromStdWString(managed)).toLower();
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
    if (!managed.empty()) return; // 已存在，无需创建

    QString drive = QString::fromStdWString(driveRoot);
    if (drive.length() == 2 && drive[1] == ':') drive += "/";
    QString defaultManaged = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
    QDir().mkpath(defaultManaged);
    qDebug() << "[AutoImport] 创建托管文件夹:" << defaultManaged;
}

std::wstring AutoImportManager::getManagedFolderAbsolutePath(const std::wstring& volSerial) {
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

    // 根据卷序列号反查当前盘符（防止盘符漂移）
    QString drive;
    for (const QFileInfo& d : QDir::drives()) {
        if (MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString()) == volSerial) {
            drive = d.absolutePath(); // 格式 "Z:/"
            break;
        }
    }
    if (drive.isEmpty()) return L"";

    // 优先从配置读取自定义路径
    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();

    // 无自定义配置则使用默认命名规则
    if (relPath.isEmpty()) {
        relPath = "ArcMeta.Library_" + drive.left(1).toUpper();
        if (!QDir(drive + relPath).exists()) return L"";
    }

    return MetadataManager::normalizePath(drive.toStdWString() + relPath.toStdWString());
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path,
                                                std::wstring& outManagedFolder) {
    outManagedFolder = getManagedFolderAbsolutePath(
        MetadataManager::getVolumeSerialNumber(path));
    return !outManagedFolder.empty();
}

} // namespace ArcMeta