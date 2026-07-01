#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
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
    qDebug() << "[DIAG] startListening 函数入口, m_isListening 当前值=" << m_isListening;  // 新增，必须在 if 判断之前
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    // 2026-07-xx 按照 Plan-120：补全对 entryUpdated 的监听，以覆盖文件移动至 Library 的场景
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    m_isListening = true;
    qDebug() << "[DIAG] startListening 已执行，信号连接完成";  // 新增 
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    qDebug() << "[DIAG] onEntryAdded 被触发, key=" << key;  // 新增 
    int idx = MftReader::instance().getIndexByKey(key);
    qDebug() << "[DIAG] getIndexByKey 返回 idx=" << idx;  // 新增 
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    qDebug() << "[DIAG] getFullPath 返回:" << qPath;  // 新增 
    std::wstring fullPath = qPath.toStdWString();
    
    // 2026-07-xx 按照 Plan-118：物理感应逻辑。检测是否在根目录下创建了符合命名的托管库文件夹
    QFileInfo info(qPath);
    if (info.isDir() && info.dir().isRoot()) {
        QString name = info.fileName();
        if (name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            qDebug() << "[AutoImport] 物理感应：识别到新托管库创建" << qPath;

            std::string fid = MetadataManager::instance().getFileIdSync(fullPath);
            if (!fid.empty()) {
                // 防重检查
                auto existingCats = CategoryRepo::getAll();
                bool exists = false;
                for (const auto& c : existingCats) { if (c.folderFid == fid) { exists = true; break; } }

                if (!exists) {
                    Category cat;
                    cat.name = name.toStdWString();
                    cat.folderFid = fid;
                    cat.pinned = true; // 托管库默认置顶常驻

                    if (CategoryRepo::add(cat)) {
                        qDebug() << "[AutoImport] 成功同步创建侧边栏分类: " << name;
                        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                    }
                }
            }
            return; // 库文件夹创建不触发入库流程
        }
    }

    std::wstring managedFolder;
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
    qDebug() << "[DIAG] checkAndGetManagedPath 结果:" << isManaged  
              << "managedFolder=" << QString::fromStdWString(managedFolder);  // 新增 
     
    if (isManaged) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    // 2026-07-xx 按照 Plan-120：逻辑与 onEntryAdded 一致，处理跨目录移动
    qDebug() << "[DIAG] onEntryUpdated 被触发, key=" << key;  // 新增 
    int idx = MftReader::instance().getIndexByKey(key);
    qDebug() << "[DIAG] getIndexByKey 返回 idx=" << idx;  // 新增 
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    qDebug() << "[DIAG] getFullPath 返回:" << qPath;  // 新增 
    std::wstring fullPath = qPath.toStdWString();

    // 2026-07-xx 按照 Plan-118：物理 -> 逻辑同步增强
    // 判定是否为库文件夹变动：1. 是文件夹；2. 位于根目录；3. 名称匹配库规范
    QFileInfo info(qPath);
    if (info.isDir() && info.dir().isRoot()) {
        QString name = info.fileName();
        if (name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            std::string fid = MetadataManager::instance().getFileIdSync(fullPath);
            if (!fid.empty()) {
                // 1. 尝试作为重命名更新 (旧库改名)
                if (CategoryRepo::updateNameByFid(fid, name.toStdWString())) {
                    qDebug() << "[AutoImport] 物理驱动重命名同步成功: FID =" << QString::fromStdString(fid)
                             << "NewName =" << name;
                    return;
                }

                // 2. 若更新未命中，尝试作为新库同步 (新建文件夹重命名为库名)
                auto existingCats = CategoryRepo::getAll();
                bool exists = false;
                for (const auto& c : existingCats) { if (c.folderFid == fid) { exists = true; break; } }
                if (!exists) {
                    Category cat;
                    cat.name = name.toStdWString();
                    cat.folderFid = fid;
                    cat.pinned = true;
                    if (CategoryRepo::add(cat)) {
                        qDebug() << "[AutoImport] 通过物理重命名感应到新库并创建分类: " << name;
                        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                    }
                }
            }
            return; // 库文件夹变动不触发入库流程
        }
    }

    std::wstring managedFolder;
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
    qDebug() << "[DIAG] checkAndGetManagedPath 结果:" << isManaged  
              << "managedFolder=" << QString::fromStdWString(managedFolder);  // 新增 

    if (isManaged) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);

        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::recordRecentVisitedFolder(const std::wstring& path) {
    if (path.empty()) return;
    // 库内文件夹不记录（没有意义，本来就在库里）
    std::wstring managedFolder;
    if (instance().checkAndGetManagedPath(path, managedFolder)) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return;

    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    QStringList list = AppConfig::instance().getValue(key, QStringList()).toStringList();

    QString qPath = QString::fromStdWString(MetadataManager::normalizePath(path));
    list.removeAll(qPath);
    list.prepend(qPath);
    while (list.size() > 14) list.removeLast();

    AppConfig::instance().setValue(key, list);
}

void AutoImportManager::scanExistingLibraries() {
    qDebug() << "[AutoImport] 开始扫描物理存量托管库...";

    // 2026-07-xx 物理对齐：在执行 MFT 搜索前，必须先同步当前在线磁盘状态
    // 理由：MftReader::search 内部会校验 m_drive_active_mask，若未同步则所有搜索结果都会被过滤
    QStringList drives;
    for (const auto& d : QDir::drives()) drives << d.absolutePath();
    MftReader::instance().updateActiveDrives(drives);

    // 1. 利用 MftReader 索引快速定位符合 ArcMeta.Library_ 命名的文件夹
    // 此处使用空 query 触发全量搜索，配合 includeDollar=false 优化性能
    auto keys = MftReader::instance().search("ArcMeta.Library_", false, false);

    int addedCount = 0;
    auto existingCats = CategoryRepo::getAll();

    for (uint64_t key : keys) {
        int idx = MftReader::instance().getIndexByKey(key);
        if (idx < 0) continue;

        if (!MftReader::instance().isDirectory(idx)) continue;

        QString fullPath = MftReader::instance().getFullPath(idx);
        QFileInfo info(fullPath);

        // 核心红线：仅识别位于根目录下的托管库
        if (info.dir().isRoot()) {
            QString name = info.fileName();
            std::string fid = MetadataManager::instance().getFileIdSync(fullPath.toStdWString());
            if (fid.empty()) continue;

            // 检查逻辑分类中是否已绑定该物理 FID
            bool alreadyRegistered = false;
            for (const auto& cat : existingCats) {
                if (cat.folderFid == fid) {
                    alreadyRegistered = true;
                    break;
                }
            }

            if (!alreadyRegistered) {
                qDebug() << "[AutoImport] 识别到存量库需同步:" << name << "FID =" << QString::fromStdString(fid);
                Category cat;
                cat.name = name.toStdWString();
                cat.folderFid = fid;
                cat.pinned = true; // 托管库默认置顶

                if (CategoryRepo::add(cat)) {
                    addedCount++;
                }
            }
        }
    }

    if (addedCount > 0) {
        qDebug() << "[AutoImport] 存量扫描同步完成，共新增分类:" << addedCount;
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
    } else {
        qDebug() << "[AutoImport] 未发现新增存量库";
    }
}

QStringList AutoImportManager::getRecentVisitedFolders(const std::wstring& volSerial) {
    if (volSerial.empty()) return QStringList();
    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    return AppConfig::instance().getValue(key, QStringList()).toStringList();
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
    // 如果传入的是路径而非序列号，则提取序列号
    if (volSerial.find(L":") != std::wstring::npos || volSerial.find(L"\\") != std::wstring::npos) {
        volSerial = MetadataManager::getVolumeSerialNumber(pathOrVolSerial);
    }
    qDebug() << "[DIAG] getManagedLibraryPath volSerial=" << QString::fromStdWString(volSerial);  // 新增 
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

    // 根据序列号反查当前盘符 (Plan-68 4.1)
    QString drive;
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        if (MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString()) == volSerial) {
            drive = d.absolutePath();
            break;
        }
    }
    qDebug() << "[DIAG] 反查得到 drive=" << drive;  // 新增 
    if (drive.isEmpty()) return L"";

    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();
    qDebug() << "[DIAG] AppConfig relPath=" << relPath;  // 新增 

    // 2026-07-xx 按照 Plan-118：约定优于配置的默认兜底
    // 若配置不存在，使用默认命名规则 ArcMeta.Library_[盘符]，
    // 但必须验证该文件夹物理存在，避免对不存在的路径做前缀匹配。
    if (relPath.isEmpty()) {
        relPath = "ArcMeta.Library_" + drive.left(1).toUpper();
        bool exists = QDir(drive + relPath).exists(); 
        qDebug() << "[DIAG] 默认兜底 relPath=" << relPath << "exists=" << exists;  // 新增 
        if (!exists) return L"";
    }

    std::wstring result = MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
    qDebug() << "[DIAG] getManagedLibraryPath 最终返回:" << QString::fromStdWString(result);  // 新增 
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
            // 2026-07-xx 按照 Plan-116：通过 USN 链路触发的入库，标记为已授权
            MetadataManager::instance().registerItem(path, true);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
