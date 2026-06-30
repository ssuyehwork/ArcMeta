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
    qDebug() << "[AIM_TRACE] startListening 函数入口, m_isListening 当前值 =" << m_isListening;
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    // 2026-07-xx 按照 Plan-120：补全对 entryUpdated 的监听，以覆盖文件移动至 Library 的场景
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    // [AIM_TRACE] 彻底修复：监听物理删除信号
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    m_isListening = true;
    qDebug() << "[AIM_TRACE] startListening 已执行，信号连接完成";
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    qDebug() << "[AIM_TRACE] onEntryAdded 被触发, key =" << key;
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    qDebug() << "[AIM_TRACE] 新增项路径解析:" << qPath;
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;
    
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
    if (isManaged) {
        qDebug() << "[AIM_TRACE] 识别到托管库内新增，加入入库队列";
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    qDebug() << "[AIM_TRACE] onEntryUpdated 被触发, key =" << key;
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;

    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
    qDebug() << "[AIM_TRACE] 路径变动解析:" << qPath << "受管状态 =" << isManaged;

    if (isManaged) {
        // [移入/原地更新] 场景
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    } else {
        // [移出] 场景彻底修复：判定该项此前是否已在库内登记
        // 注意：此处需要利用 MetadataManager 的 FID 反查能力
        std::string fid = MetadataManager::instance().getFileIdSync(fullPath);
        std::wstring lastKnownPath = MetadataManager::instance().getPathByFid(fid);

        if (!lastKnownPath.empty() && MetadataManager::isInsideManagedLibrary(lastKnownPath)) {
            qDebug() << "[AIM_TRACE] 项目已从库内移出至:" << qPath << "执行数据库同步注销";
            MetadataManager::instance().deletePermanently(lastKnownPath);
        }
    }
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    qDebug() << "[AIM_TRACE] onEntryRemoved 被触发, key =" << key;
    // 彻底修复：处理物理删除
    // 此时 MFT 已移除该项，无法通过 MftReader 获取路径。
    // 但可以通过复合 Key 提取 FRN，并结合 MetadataManager 的 FID 反查（前提是之前已缓存路径）

    // 方案：由于 MetadataManager 持有 fidToPath 映射，而 USN 的复合 key 包含盘符索引
    // 此处简化处理：由于文件已不存在，我们直接触发 MetadataManager 的全库对账清理或精准反查
    // 工业级补全：利用 MetadataManager::getPathByFid 寻找可能残留的该 FRN 记录

    // 提取盘符索引和 FRN
    size_t dIdx = static_cast<size_t>(key >> 48);
    uint64_t frnVal = key & 0x0000FFFFFFFFFFFFull;

    // 构建 FID
    std::wstring volSerial = L"UNKNOWN";
    const auto drives = QDir::drives();
    int curIdx = 0;
    for (const QFileInfo& d : drives) {
        if (curIdx == (int)dIdx) {
            volSerial = MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString());
            break;
        }
        curIdx++;
    }

    wchar_t frnBuf[17];
    swprintf(frnBuf, 17, L"%016llX", frnVal);
    std::string fid = MetadataManager::generateFallbackFid(volSerial, frnBuf);

    std::wstring lastKnownPath = MetadataManager::instance().getPathByFid(fid);
    if (!lastKnownPath.empty()) {
        bool inLib = MetadataManager::isInsideManagedLibrary(lastKnownPath);
        qDebug() << "[AIM_TRACE] 感知到物理删除: 路径 =" << QString::fromStdWString(lastKnownPath) << "库内 =" << inLib;
        if (inLib) {
            MetadataManager::instance().deletePermanently(lastKnownPath);
        }
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

QStringList AutoImportManager::getRecentVisitedFolders(const std::wstring& volSerial) {
    if (volSerial.empty()) return QStringList();
    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    return AppConfig::instance().getValue(key, QStringList()).toStringList();
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring managedAbs = getManagedLibraryPath(path);
    if (managedAbs.empty()) return false;

    bool match = (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0);
    if (match) {
        outManagedFolder = managedAbs;
    }
    return match;
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
