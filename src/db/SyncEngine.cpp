#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "SyncEngine.h"
#include "Database.h"
#include "ItemRepo.h"
#include "FolderRepo.h"
#include "../meta/MetadataDefs.h"
#include "../meta/MetadataManager.h"
#include "../meta/AmMetaJson.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QtConcurrent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <filesystem>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "../core/CoreController.h"


namespace ArcMeta {

/**
 * @brief 通过 FID 反查物理路径 (Windows API)
 */
static std::wstring resolveFidToPath(const std::string& fidStr) {
    size_t dashPos = fidStr.find('-');
    if (dashPos == std::string::npos) return L"";

    std::wstring volSerial = QString::fromStdString(fidStr.substr(0, dashPos)).toStdWString();
    std::string hexId = fidStr.substr(dashPos + 1);
    if (hexId.length() != 32) return L"";

    // 1. 寻找匹配卷序列号的驱动器
    wchar_t driveLetter = 0;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            wchar_t root[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            DWORD serial = 0;
            if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
                wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial);
                if (volSerial == buf) { driveLetter = L'A' + i; break; }
            }
        }
    }
    if (driveLetter == 0) return L"";

    // 2. 使用 OpenFileById 反查
    std::wstring driveRoot = std::wstring(1, driveLetter) + L":\\";
    HANDLE hVol = CreateFileW(driveRoot.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return L"";

    FILE_ID_DESCRIPTOR desc;
    desc.dwSize = sizeof(desc);
    desc.Type = ExtendedFileIdType; // 128-bit
    
    // 2026-05-10 修复：直接内存操作设置FileId
    unsigned char* descPtr = (unsigned char*)&desc;
    // 跳过dwSize(4)和Type(4)字段，直接操作FileId部分
    unsigned char* fileIdPtr = descPtr + 8;
    for (int i = 0; i < 16; ++i) {
        std::string byteStr = hexId.substr(i * 2, 2);
        fileIdPtr[15 - i] = (unsigned char)std::stoul(byteStr, nullptr, 16);
    }

    HANDLE hFile = OpenFileById(hVol, &desc, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
    CloseHandle(hVol);

    if (hFile == INVALID_HANDLE_VALUE) return L"";

    wchar_t pathBuf[MAX_PATH * 4];
    DWORD len = GetFinalPathNameByHandleW(hFile, pathBuf, MAX_PATH * 4, FILE_NAME_NORMALIZED);
    CloseHandle(hFile);

    if (len > 0 && len < MAX_PATH * 4) {
        std::wstring result(pathBuf);
        if (result.find(L"\\\\?\\") == 0) return result.substr(4);
        return result;
    }
    return L"";
}

SyncEngine::SyncEngine(QObject* parent) : QObject(parent) {}

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

void SyncEngine::runIncrementalSync(std::function<void()> onFinished) {
    auto& mgr = MetadataManager::instance();
    if (!mgr.hasPendingSync()) {
        if (onFinished) onFinished();
        return;
    }

    emit syncStatusChanged(true);

    // 2026-06-16 按照用户要求：从事务日志读取 FID -> 同步物理 JSON 到数据库 -> 核实后清空
    (void)QtConcurrent::run(&CoreController::instance().backgroundPool(), [this, onFinished]() {
        auto& mgr = MetadataManager::instance();
        QStringList pendingFids = mgr.getPendingSyncDirs();
        QStringList remainingFids;
        
        qDebug() << "[Sync] 开始执行 FID 驱动型对账同步，任务数:" << pendingFids.size();

        for (const QString& fidItem : pendingFids) {
            std::wstring targetPath;
            std::string fidStr = fidItem.toStdString();
            
            // 1. 尝试解析 FID 为物理路径
            if (fidStr.find('-') != std::string::npos && fidStr.length() > 30) {
                targetPath = resolveFidToPath(fidStr);
            } else {
                targetPath = fidItem.toStdWString(); // 路径回退模式
            }

            if (targetPath.empty() || !QFile::exists(QString::fromStdWString(targetPath))) {
                qDebug() << "[Sync] 物理路径已失效或不存在，自动清理任务:" << fidItem;
                // 不加入 remainingFids，即视为“已处理”以便从日志移除
                continue;
            }

            // 2. 执行物理 JSON 到数据库的对账同步
            QFileInfo fi(QString::fromStdWString(targetPath));
            std::wstring syncDir;
            if (fi.isDir()) {
                syncDir = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
            } else {
                syncDir = QDir::toNativeSeparators(fi.absolutePath()).toStdWString();
            }

            AmMetaJson amJson(syncDir);
            if (amJson.load()) {
                bool ok = true;
                // A. 同步文件夹自身元数据
                if (!amJson.folder().isDefault()) {
                    FolderRepo::save(MetadataManager::getVolumeSerialNumber(syncDir), syncDir, amJson.folder());
                }

                // B. 同步目录下所有项
                auto const& itemsMap = amJson.items();
                for (auto it = itemsMap.begin(); it != itemsMap.end(); ++it) {
                    const std::wstring& name = it->first;
                    const ItemMeta& item = it->second;

                    std::wstring fullPath = syncDir + L"\\" + name;
                    ItemMeta iMeta;
                    MetadataManager::instance().fetchWinApiMetadataDirect(fullPath, iMeta.fileId128, &iMeta.frn, &iMeta.size, &iMeta.type, &iMeta.creationTime, &iMeta.modificationTime, &iMeta.accessTime);
                    iMeta.rating = item.rating; iMeta.color = item.color;
                    iMeta.pinned = item.pinned; iMeta.note = item.note;
                    iMeta.encrypted = item.encrypted;
                    iMeta.tags = item.tags;
                    iMeta.volume = MetadataManager::getVolumeSerialNumber(syncDir);
                    
                    if (!ItemRepo::save(syncDir, name, iMeta)) ok = false;
                }

                if (!ok) {
                    qWarning() << "[Sync] 部分条目入库失败，任务将重试:" << fidItem;
                    remainingFids << fidItem;
                }
            } else {
                qDebug() << "[Sync] 找不到有效的 .am_meta.json，清理冗余任务:" << fidItem;
                // 无法加载通常意味着文件不存在或损坏，不再阻塞同步
            }
        }

        // 3. 立即验证并原子化写回日志 (仅移除已成功的 FID)
        QStringList successfulFids;
        for (const auto& fid : pendingFids) {
            if (!remainingFids.contains(fid)) successfulFids << fid;
        }
        mgr.removeFidsFromLog(successfulFids);

        qDebug() << "[Sync] 对账同步结束，剩余任务:" << remainingFids.size();
        
        QMetaObject::invokeMethod(this, [this, onFinished]() {
            emit syncStatusChanged(false);
            if (onFinished) onFinished();
        }, Qt::QueuedConnection);
    });
}

bool SyncEngine::hasPendingTasks() const {
    return MetadataManager::instance().hasPendingSync();
}

/**
 * @brief 全量扫描：2026-06-xx 架构变更
 * 彻底移除 .am_meta.json 之后，不再扫描物理文件。全量同步转为对已有关联数据的完整性校验。
 */
void SyncEngine::runFullScan(const std::vector<std::wstring>& drivesToScanInput, 
                             std::function<void(int current, int total, const std::wstring& path)> onProgress) {
    Q_UNUSED(drivesToScanInput);
    Q_UNUSED(onProgress);
    qDebug() << "[Sync] 全量物理文件扫描功能已随 .am_meta.json 移除而废弃。";
}

void SyncEngine::rebuildTagStats() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    if (!db.isOpen()) return;
    
    db.transaction();
    QSqlQuery qDelete(db);
    qDelete.exec("DELETE FROM tags");
    
    QSqlQuery query("SELECT tags FROM items WHERE tags != ''", db);
    std::map<std::string, int> tagCounts;
    while (query.next()) {
        QByteArray jsonData = query.value(0).toByteArray();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isArray()) {
            for (const auto& val : doc.array()) {
                QString t = val.toString();
                if (!t.isEmpty()) tagCounts[t.toStdString()]++;
            }
        }
    }
    for (auto const& [tag, count] : tagCounts) {
        QSqlQuery ins(db);
        ins.prepare("INSERT INTO tags (tag, item_count) VALUES (?, ?)");
        ins.addBindValue(QString::fromStdString(tag));
        ins.addBindValue(count);
        ins.exec();
    }
    db.commit();
}

void SyncEngine::scanDirectory(const std::filesystem::path& root, std::vector<std::wstring>& metaFiles) {
    // 2026-06-xx 已废弃
    Q_UNUSED(root);
    Q_UNUSED(metaFiles);
}

} // namespace ArcMeta
