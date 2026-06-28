#include <QFileInfo>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QThreadPool>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QImageReader>
#include <QSvgRenderer>
#ifdef Q_OS_WIN
#include <objbase.h>
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MetadataManager.h"
#include "MetadataDefs.h"
#include "DatabaseManager.h"
#include "../mft/MftReader.h"
#include "../meta/CategoryRepo.h"
#include "../ui/UiHelper.h"
#include "sqlite3.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <windows.h>
#include <objbase.h>
#include <fileapi.h>
#include <winbase.h>
#include <handleapi.h>
#include <winnt.h>
#include <sddl.h>


#include <cstdio>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

namespace ArcMeta {

// --- Helper Functions ---

std::wstring MetadataManager::normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    // 2026-06-xx 物理对账优化：Windows 环境下路径不区分大小写，
    // 统一转换为全小写以确保内存缓存 (std::unordered_map) 的 Key 匹配一致性，彻底消除“幽灵项”。
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path))).toLower();
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    return qp.toStdWString();
}

std::string MetadataManager::generateFallbackFid(const std::wstring& vol, const std::wstring& frn) {
    if (vol.empty() || frn.empty()) return "";
    return "FRN:" + QString::fromStdWString(vol).toUpper().toStdString() + ":" + QString::fromStdWString(frn).toUpper().toStdString();
}

std::string MetadataManager::generateDeterministicSha256Id(const std::wstring& path) {
    if (path.empty()) return "";
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring vol = MetadataManager::getVolumeSerialNumber(nPath);
    QByteArray seed = QString::fromStdWString(vol + L":" + nPath).toUtf8();
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    return "PATHURL:" + hash.left(16).toHex().toUpper().toStdString();
}

std::wstring MetadataManager::generateDeterministicFrn(const std::wstring& path) {
    if (path.empty()) return L"VIRTUAL_EMPTY";
    QByteArray hash = QCryptographicHash::hash(QString::fromStdWString(path).toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.left(8).toHex().toUpper()).toStdWString();
}

// --- MetadataManager Implementation ---

MetadataManager& MetadataManager::instance() {
    static MetadataManager inst;
    return inst;
}

MetadataManager::MetadataManager(QObject* parent) : QObject(parent) {
    m_batchTimer = new QTimer(this);
    m_batchTimer->setInterval(1500);
    m_batchTimer->setSingleShot(true);

    m_uiSignalTimer = new QTimer(this);
    m_uiSignalTimer->setInterval(200); // 200ms 时间窗口
    m_uiSignalTimer->setSingleShot(true);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(5000); // 每 5 秒尝试一次补偿
    connect(m_retryTimer, &QTimer::timeout, this, &MetadataManager::processVisualRetryQueue);
    connect(m_uiSignalTimer, &QTimer::timeout, [this]() {
        std::vector<QString> paths;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_pendingUiPaths) paths.push_back(p);
            m_pendingUiPaths.clear();
        }

        for (const auto& p : paths) {
            // 语义化特殊信号依然直接发射以确保优先级
            if (p.startsWith("__RELOAD_")) {
                emit metaChanged(p);
            } else {
                emit metaChanged(p);
            }
        }
    });

    connect(m_batchTimer, &QTimer::timeout, [this]() {
        std::vector<std::wstring> paths;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_dirtyPaths) {
                paths.push_back(p);
            }
            m_dirtyPaths.clear();
        }
        
        // 2026-06-xx 性能优化：持久化任务切入后台线程池，杜绝主线程 I/O 挂起
        if (!paths.empty()) {
            (void)QtConcurrent::run([this, paths]() {
                for (const auto& p : paths) {
                    persistAsync(p);
                }
            });
        }
    });

    // 2026-06-xx 物理加固：监听程序退出信号，确保内存中的元数据变更落盘
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
        qDebug() << "[Metadata] 程序退出前强制保存所有脏数据...";
        std::vector<std::wstring> paths;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_dirtyPaths) paths.push_back(p);
            m_dirtyPaths.clear();
        }
        for (const auto& p : paths) persistAsync(p);
        
        // 2026-06-xx 物理切换：强制刷新 SQLite 到磁盘
        DatabaseManager::instance().flushAll();
    });
}


void MetadataManager::initFromScchMode() {
    // 2026-06-xx 物理加固：防止重复初始化
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_loaded) return;
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    DatabaseManager::instance().init();

    qDebug() << "[PERF] 正在从 SQLite 内存模式初始化元数据缓存...";
    
    std::unordered_map<std::wstring, RuntimeMeta> tempCache;
    std::unordered_map<std::string, std::wstring> tempFidToPath;

    auto loadFromDb = [&](sqlite3* db) {
        if (!db) return;
        sqlite3_stmt* stmt;
        const char* sql = "SELECT * FROM metadata";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                RuntimeMeta rm;
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (fid) rm.fileId128 = fid;

                const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                std::wstring path = normalizePath(wpath ? wpath : L"");

                rm.isFolder = sqlite3_column_int(stmt, 2);
                rm.rating = sqlite3_column_int(stmt, 3);
                const wchar_t* color = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
                if (color) rm.color = color;
                
                const wchar_t* wtags = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 5));
                QString tags = wtags ? QString::fromWCharArray(wtags) : "";
                rm.tags = tags.split(",", Qt::SkipEmptyParts);

                const wchar_t* note = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 6));
                if (note) rm.note = note;
                const wchar_t* url = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 7));
                if (url) rm.url = url;
                rm.ctime = sqlite3_column_int64(stmt, 8);
                rm.mtime = sqlite3_column_int64(stmt, 9);
                rm.atime = sqlite3_column_int64(stmt, 10);
                rm.fileSize = sqlite3_column_int64(stmt, 11);

                const void* paletteBlob = sqlite3_column_blob(stmt, 12);
                int paletteSize = sqlite3_column_bytes(stmt, 12);

                rm.isTrash = sqlite3_column_int(stmt, 13) != 0;
                const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 14));
                if (wOrigPath) rm.originalPath = wOrigPath;
                rm.isInvalid = sqlite3_column_int(stmt, 15) != 0;
                rm.width = sqlite3_column_int(stmt, 16);
                rm.height = sqlite3_column_int(stmt, 17);
                rm.ingestionStatus = sqlite3_column_int(stmt, 18);
                if (paletteBlob && paletteSize > 0) {
                    QByteArray ba(reinterpret_cast<const char*>(paletteBlob), paletteSize);
                    QJsonDocument doc = QJsonDocument::fromJson(ba);
                    QJsonArray arr = doc.array();
                    for (const auto& v : arr) {
                        QJsonObject obj = v.toObject();
                        PaletteEntry pe;
                        pe.color = QColor(obj["color"].toString());
                        pe.ratio = (float)obj["ratio"].toDouble();
                        rm.palettes.push_back(pe);
                    }
                }

                rm.isManaged = true;
                tempCache[path] = rm;
                if (!rm.fileId128.empty()) tempFidToPath[rm.fileId128] = path;
            }
            sqlite3_finalize(stmt);
        }
    };

    // 0. 加载全局库 (盘符置顶等全局元数据)
    loadFromDb(DatabaseManager::instance().getGlobalDb());

    // 1. 扫描所有已加载的数据库
    // 2026-06-xx 逻辑加固：由于驱动器序列号在不同机器上可能重复或变化，
    // 我们必须确保启动时扫描 .arcmeta 目录下所有物理分库。
    QString metaDir = QCoreApplication::applicationDirPath() + "/.arcmeta";
    QDir dir(metaDir);
    if (dir.exists()) {
        QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files | QDir::Hidden | QDir::System);
        qDebug() << "[Metadata] 发现物理分库数量:" << dbFiles.size();

        // 使用正则解析：^Arcmeta_([0-9A-F]{8})(?:_([A-Z]))?\.db$
        QRegularExpression re("^Arcmeta_([0-9A-F]{8})(?:_([A-Z]))?\\.db$", QRegularExpression::CaseInsensitiveOption);
        std::set<std::wstring> loadedSerials;

        // 构建当前在线磁盘的 序列号 -> 盘符 映射，用于初始化时的自适应重命名
        QMap<std::wstring, QString> serialToLetter;
        const auto drives = QDir::drives();
        for (const QFileInfo& d : drives) {
            std::wstring s = getVolumeSerialNumber(d.absolutePath().toStdWString());
            if (s != L"UNKNOWN") {
                serialToLetter[s] = d.absolutePath().at(0).toUpper();
            }
        }

        for (const QString& dbFile : dbFiles) {
            QRegularExpressionMatch match = re.match(dbFile);
            if (match.hasMatch()) {
                QString volSerialStr = match.captured(1).toUpper();
                std::wstring wSerial = volSerialStr.toStdWString();
                
                if (loadedSerials.find(wSerial) == loadedSerials.end()) {
                    // 启动阶段：若检测到该序列号的磁盘当前在线，则传入盘符触发自适应重命名
                    QString currentLetter = serialToLetter.value(wSerial, "");
                    loadFromDb(DatabaseManager::instance().getMemoryDb(wSerial, currentLetter));
                    loadedSerials.insert(wSerial);
                }
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = tempCache;
        m_fidToPath = tempFidToPath;

        // 2026-07-xx 物理同步：初始化时构建所有已加载卷的隔离索引
        for (const auto& pair : m_cache) {
            const std::wstring& path = pair.first;
            const RuntimeMeta& meta = pair.second;
            std::wstring name, ext;
            parsePathComponents(path, meta.isFolder, name, ext);
            if (!name.empty()) {
                if (meta.isFolder) {
                    auto& v = m_folderNameToFids[name];
                    if (std::find(v.begin(), v.end(), meta.fileId128) == v.end()) v.push_back(meta.fileId128);
                } else {
                    auto& v = m_fileNameToFids[name];
                    if (std::find(v.begin(), v.end(), meta.fileId128) == v.end()) v.push_back(meta.fileId128);
                    if (!ext.empty()) {
                        auto& ve = m_extensionToFids[ext];
                        if (std::find(ve.begin(), ve.end(), meta.fileId128) == ve.end()) ve.push_back(meta.fileId128);
                    }
                }
            }
        }

        m_loaded = true;
    }

    // 2026-06-xx 物理对账：在初始化结束后（m_loaded 为 true 且缓存就绪），执行一次完整的统计重计
    CategoryRepo::fullRecount();
    qDebug() << "[PERF] SQLite 元数据镜像构建完成。内存映射数:" << tempCache.size() 
             << " ID索引数:" << tempFidToPath.size()
             << " 耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
    notifyUI(RefreshLevel::FullRebuild);
}

void MetadataManager::notifyUI(RefreshLevel level, const QString& path) {
    switch (level) {
        case RefreshLevel::CountsOnly:
            notifyCategoryCountChanged();
            break;
        case RefreshLevel::PathUpdate:
            if (!path.isEmpty()) {
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_pendingUiPaths.insert(path);
                }
                QMetaObject::invokeMethod(m_uiSignalTimer, "start", Qt::QueuedConnection);
            }
            break;
        case RefreshLevel::FullRebuild:
            notifyFullUIRebuild();
            break;
    }
}

void MetadataManager::notifyCategoryCountChanged() {
    if (m_isInternalOperating) return; // 2026-xx-xx 按照 Plan-105：操作期间拦截冗余刷新信号
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_pendingUiPaths.insert("__RELOAD_COUNT__");
    }
    QMetaObject::invokeMethod(m_uiSignalTimer, "start", Qt::QueuedConnection);
}

void MetadataManager::notifyFullUIRebuild() {
    if (m_isInternalOperating) return; // 2026-xx-xx 按照 Plan-105：操作期间拦截冗余刷新信号
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_pendingUiPaths.insert("__RELOAD_ALL__");
    }
    QMetaObject::invokeMethod(m_uiSignalTimer, "start", Qt::QueuedConnection);
}

void MetadataManager::registerItem(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    // 1. 激活项目 (获取 FID/FRN 等物理属性)
    ensureActivated(nPath);
    // 2. 提取图像尺寸 (Plan-29)
    tryExtractDimensions(nPath);

    // 2026-11-xx 按照 Plan-113：登记初值必须为 Registered (0)
    // 仅在视觉解析 (tryExtractColor) 完成后才由后台任务流转为 Ingested (1)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nPath].ingestionStatus = 0;
    }

    // 3. 物理同步 (存入数据库)
    syncPhysicalMetadata(nPath, false);
    // 4. 视觉预热 (提取颜色)
    tryExtractColor(nPath);
    // 5. 通知 UI 刷新该路径
    notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}

void MetadataManager::registerItemsAsync(const QStringList& paths) {
    if (paths.isEmpty()) return;
    
    // 2026-07-xx 按照 Plan-88：全异步批量注册链
    (void)QtConcurrent::run([this, paths]() {
#ifdef Q_OS_WIN
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // 赋予 Shell/图像分析能力
#endif
        for (const auto& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            
            // 1. 激活 (优化版，内含锁分离 I/O)
            ensureActivated(nPath);
            
            // 2. 标记为 Registered (0)
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache[nPath].ingestionStatus = 0;
            }

            // 3. 物理与视觉属性提取 (耗时操作)
            tryExtractDimensions(nPath);
            syncPhysicalMetadata(nPath, false); // 内部已异步化
            tryExtractColor(nPath);
            
            // 4. 增量通知 UI
            notifyUI(RefreshLevel::PathUpdate, qp);
        }
#ifdef Q_OS_WIN
        CoUninitialize();
#endif
    });
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end()) return it->second;
    }
    return RuntimeMeta();
}

std::wstring MetadataManager::getPathByFid(const std::string& fid) {
    if (fid.empty()) return L"";
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_fidToPath.find(fid);
    return (it != m_fidToPath.end()) ? it->second : L"";
}

void MetadataManager::ensureActivated(const std::wstring& nPath) {
    // 1. 读锁检查 (快速路径)
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.find(nPath) != m_cache.end()) return;
    }

    // 2. 锁外同步获取物理属性 (耗时 I/O 操作)
    // 2026-07-xx 按照 Plan-88：杜绝在 unique_lock 期间执行 Win32 API 访问
    RuntimeMeta rm;
    std::wstring frn;
    std::wstring type;
    if (fetchWinApiMetadataDirect(nPath, rm.fileId128, &frn, &rm.fileSize, &type, &rm.ctime, &rm.mtime, &rm.atime)) {
        rm.isFolder = (type == L"folder");
        
        // 3. 写锁写入缓存
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.count(nPath)) return; // 二次检查防止竞态

        // 共享元数据逻辑 (FID 关联)
        if (!rm.fileId128.empty() && m_fidToPath.count(rm.fileId128)) {
            const RuntimeMeta& existing = m_cache[m_fidToPath[rm.fileId128]];
            rm.rating    = existing.rating;
            rm.color     = existing.color;
            rm.tags      = existing.tags;
            rm.note      = existing.note;
            rm.url       = existing.url;
            rm.width     = existing.width;
            rm.height    = existing.height;
            rm.palettes  = existing.palettes;
            rm.isManaged = existing.isManaged;
        }

        // 2026-11-xx 按照 Plan-113：原地复活机制
        // 若该 FID 之前处于失效状态 (-1)，且当前路径位于托管库，则自动恢复为已入库 (1)
        if (!rm.fileId128.empty()) {
            auto itFid = m_fidToPath.find(rm.fileId128);
            if (itFid != m_fidToPath.end()) {
                RuntimeMeta& oldMeta = m_cache[itFid->second];
                if (oldMeta.ingestionStatus == -1) {
                    rm.rating = oldMeta.rating;
                    rm.color = oldMeta.color;
                    rm.tags = oldMeta.tags;
                    rm.note = oldMeta.note;
                    rm.url = oldMeta.url;
                    rm.width = oldMeta.width;
                    rm.height = oldMeta.height;
                    rm.palettes = oldMeta.palettes;
                    rm.isManaged = oldMeta.isManaged;
                    rm.ingestionStatus = 1; // 恢复为已入库
                    qDebug() << "[Metadata] FRN 追踪：检测到失效项移回，自动复活元数据:" << QString::fromStdWString(nPath);
                }
            }
        }

        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) {
            m_fidToPath[rm.fileId128] = nPath;

            // 索引同步逻辑
            std::wstring name, ext;
            parsePathComponents(nPath, rm.isFolder, name, ext);
            if (!name.empty()) {
                if (rm.isFolder) {
                    auto& v = m_folderNameToFids[name];
                    if (std::find(v.begin(), v.end(), rm.fileId128) == v.end()) v.push_back(rm.fileId128);
                } else {
                    auto& v = m_fileNameToFids[name];
                    if (std::find(v.begin(), v.end(), rm.fileId128) == v.end()) v.push_back(rm.fileId128);
                    if (!ext.empty()) {
                        auto& ve = m_extensionToFids[ext];
                        if (std::find(ve.begin(), ve.end(), rm.fileId128) == ve.end()) ve.push_back(rm.fileId128);
                    }
                }
            }
        }
    }
}

void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        m_cache[nPath].rating = rating; 
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

QString MetadataManager::getDriveLetterByMftIndex(int driveIdx) {
    return MftReader::instance().getDriveLetter(driveIdx);
}

uint64_t MetadataManager::getLastUsn(const std::wstring& volume) {
    std::wstring serial = getVolumeSerialNumber(volume);
    if (serial == L"UNKNOWN") return 0;
    
    sqlite3* db = DatabaseManager::instance().getMemoryDb(serial);
    return (uint64_t)DatabaseManager::instance().getSystemStat(db, "last_usn", 0);
}

void MetadataManager::setLastUsn(const std::wstring& volume, uint64_t usn) {
    std::wstring serial = getVolumeSerialNumber(volume);
    if (serial == L"UNKNOWN") return;

    sqlite3* db = DatabaseManager::instance().getMemoryDb(serial);
    DatabaseManager::instance().setSystemStat(db, "last_usn", (long long)usn);
}

void MetadataManager::renameTag(const QString& oldName, const QString& newName) {
    if (oldName == newName) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (auto& pair : m_cache) {
        if (pair.second.tags.contains(oldName)) {
            pair.second.tags.removeAll(oldName);
            if (!newName.isEmpty() && !pair.second.tags.contains(newName)) {
                pair.second.tags.append(newName);
            }
            pushToDirty_NoLock(pair.first);
        }
    }
    QMetaObject::invokeMethod(m_batchTimer, "start", Qt::QueuedConnection);
    notifyFullUIRebuild();
}

void MetadataManager::removeTag(const QString& tagName) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (auto& pair : m_cache) {
        if (pair.second.tags.contains(tagName)) {
            pair.second.tags.removeAll(tagName);
            pushToDirty_NoLock(pair.first);
        }
    }
    QMetaObject::invokeMethod(m_batchTimer, "start", Qt::QueuedConnection);
    notifyFullUIRebuild();
}

void MetadataManager::setInvalid(const std::wstring& path, bool invalid, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    bool changed = false;
    bool isManaged = false;
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        if (m_cache[nPath].isInvalid != invalid) {
            m_cache[nPath].isInvalid = invalid; 
            changed = true;
            isManaged = m_cache[nPath].isManaged;
        }
    }
    
    if (changed) {
        // 2026-07-xx 物理修复：仅当项已登记 (isManaged) 时，其失效状态变更才影响活跃总数
        if (isManaged) {
            CategoryRepo::incrementTotalFileCount(invalid ? -1 : 1);
        }
        if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
        debouncePersist(nPath);
    }
}

void MetadataManager::setInvalidByFidPrefix(const std::string& fidPrefix, bool invalid) {
    // 2026-06-26 按照 Plan-108：基于 FID 前缀批量标记失效
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::vector<std::wstring> affectedPaths;

    for (auto& pair : m_cache) {
        if (pair.second.fileId128.find(fidPrefix) == 0) {
            if (pair.second.isInvalid != invalid || (invalid && pair.second.ingestionStatus != -1)) {
                pair.second.isInvalid = invalid;
                // 2026-11-xx 按照 Plan-113：物理移除时同步更新 ingestionStatus 为 -1
                if (invalid) pair.second.ingestionStatus = -1; 
                
                if (pair.second.isManaged) {
                    CategoryRepo::incrementTotalFileCount(invalid ? -1 : 1);
                }
                affectedPaths.push_back(pair.first);
            }
        }
    }

    lock.unlock();
    for (const auto& p : affectedPaths) {
        debouncePersist(p);
    }
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        m_cache[nPath].color = color; 
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].pinned = pinned; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nPath].tags = tags;
    }

    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].note = note; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].url = url; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].encrypted = encrypted; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setManaged(const std::wstring& path, bool managed, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].isManaged = managed; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    // 2026-07-xx 逻辑校准：isManaged 是由数据库持久化驱动的标记。
    // 如果显式设为 true，则发起一次持久化以确保入库；如果是设为 false（罕见），无需特殊持久化。
    if (managed) debouncePersist(nPath); 
}

void MetadataManager::setIngestionStatus(const std::wstring& path, int status, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nPath].ingestionStatus = status;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].palettes = entries; }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        RuntimeMeta& meta = m_cache[nPath];
        meta.color = color;
        meta.palettes = entries;
        // 2026-11-xx 按照 Plan-113：视觉解析完成，流转为 Ingested (1)
        if (meta.ingestionStatus == 0) {
            meta.ingestionStatus = 1;
        }
    }
    
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

QVector<QColor> MetadataManager::getPalettes(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end() && !it->second.palettes.empty()) {
            QVector<QColor> colors;
            for (const auto& entry : it->second.palettes) colors << entry.color;
            return colors;
        }
    }
    return {};
}

void MetadataManager::debouncePersist(const std::wstring& nPath) {
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_dirtyPaths.insert(nPath); }
    QMetaObject::invokeMethod(m_batchTimer, "start", Qt::QueuedConnection);
}

void MetadataManager::pushToDirty_NoLock(const std::wstring& nPath) {
    m_dirtyPaths.insert(nPath);
}

void MetadataManager::renameItem(const std::wstring& oldPath, const std::wstring& newPath) {
    std::wstring nOld = normalizePath(oldPath);
    std::wstring nNew = normalizePath(newPath);
    if (nOld == nNew) return;

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nOld);
        if (it != m_cache.end()) { 
            std::string fid = it->second.fileId128;
            bool isFolder = it->second.isFolder;

            // 1. 移除旧名称/后缀索引
            std::wstring oldName, oldExt;
            parsePathComponents(nOld, isFolder, oldName, oldExt);
            if (!oldName.empty()) {
                if (isFolder) {
                    auto& v = m_folderNameToFids[oldName];
                    v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                    if (v.empty()) m_folderNameToFids.erase(oldName);
                } else {
                    auto& v = m_fileNameToFids[oldName];
                    v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                    if (v.empty()) m_fileNameToFids.erase(oldName);
                    if (!oldExt.empty()) {
                        auto& ve = m_extensionToFids[oldExt];
                        ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                        if (ve.empty()) m_extensionToFids.erase(oldExt);
                    }
                }
            }

            m_cache[nNew] = it->second; 
            m_cache.erase(it); 
            if (!fid.empty()) {
                m_fidToPath[fid] = nNew;

                // 2. 注册新名称/后缀索引
                std::wstring newName, newExt;
                parsePathComponents(nNew, isFolder, newName, newExt);
                if (!newName.empty()) {
                    if (isFolder) {
                        auto& v = m_folderNameToFids[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                    } else {
                        auto& v = m_fileNameToFids[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                        if (!newExt.empty()) {
                            auto& ve = m_extensionToFids[newExt];
                            if (std::find(ve.begin(), ve.end(), fid) == ve.end()) ve.push_back(fid);
                        }
                    }
                }
            }

            // 物理同步：更新 SQLite 路径
            std::wstring volSerial = getVolumeSerialNumber(nNew);
            QString letter = (nNew.length() >= 2 && nNew[1] == L':') ? QString::fromWCharArray(&nNew[0], 1) : "";
            sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial, letter);
            if (db) {
                sqlite3_stmt* stmt;
                const char* sql = "UPDATE metadata SET path = ? WHERE file_id = ?";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text16(stmt, 1, nNew.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
    }
    notifyFullUIRebuild();
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring volSerial = getVolumeSerialNumber(nPath);
    QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : "";
    sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial, letter);
    
    int totalDelta = 0;
    std::vector<std::string> fids;
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == nPath || it->first.find(nPath + L"\\") == 0 || it->first.find(nPath + L"/") == 0) {
                // 2026-07-xx 物理修正：回收站项目已在移入时预扣减，
                // 此处物理删除时，仅对“活跃”（非回收站且非失效）的项目执行扣减。
                if (!it->second.isFolder && !it->second.isInvalid && !it->second.isTrash) {
                    totalDelta--;
                }
                if (!it->second.fileId128.empty()) {
                    std::string fid = it->second.fileId128;
                    bool isFolder = it->second.isFolder;
                    fids.push_back(fid);
                    m_fidToPath.erase(fid);

                    // 2026-07-xx 隔离索引同步：移除删除项
                    std::wstring name, ext;
                    parsePathComponents(it->first, isFolder, name, ext);
                    if (!name.empty()) {
                        if (isFolder) {
                            auto& v = m_folderNameToFids[name];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_folderNameToFids.erase(name);
                        } else {
                            auto& v = m_fileNameToFids[name];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_fileNameToFids.erase(name);
                            if (!ext.empty()) {
                                auto& ve = m_extensionToFids[ext];
                                ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                                if (ve.empty()) m_extensionToFids.erase(ext);
                            }
                        }
                    }
                }
                it = m_cache.erase(it);
            }
            else ++it;
        }
    }

    // 2026-06-xx 物理级根除：基于 File ID (FRN) 批量清理，确保即使路径发生偏移（如在回收站中）也能彻底删除
    if (db && !fids.empty()) {
        sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE file_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    }

    if (totalDelta != 0) CategoryRepo::incrementTotalFileCount(totalDelta);
    
    // 2026-06-xx 物理级根除：基于 File ID (FRN) 批量清理所有分类关联，彻底杜绝“幽灵关联”
    if (!fids.empty()) {
        CategoryRepo::removeAllCategoriesBatch(fids);
    }
}

void MetadataManager::markAsTrash(const std::wstring& path, bool isTrash, const std::wstring& origPath) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::string fid;
    fetchWinApiMetadataDirect(nPath, fid);

    bool changed = false;
    bool isManaged = false;
    bool isInvalid = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        // 核心修复：防止内存中出现同一个 FID 的多条路径记录（物理偏移导致的重复计数）
        if (!fid.empty() && m_fidToPath.count(fid)) {
            std::wstring oldPath = m_fidToPath[fid];
            if (oldPath != nPath) {
                // 在清理旧路径前，同步清理隔离索引
                auto itOld = m_cache.find(oldPath);
                if (itOld != m_cache.end()) {
                    std::wstring oldName, oldExt;
                    parsePathComponents(oldPath, itOld->second.isFolder, oldName, oldExt);
                    if (!oldName.empty()) {
                        if (itOld->second.isFolder) {
                            auto& v = m_folderNameToFids[oldName];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_folderNameToFids.erase(oldName);
                        } else {
                            auto& v = m_fileNameToFids[oldName];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_fileNameToFids.erase(oldName);
                            if (!oldExt.empty()) {
                                auto& ve = m_extensionToFids[oldExt];
                                ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                                if (ve.empty()) m_extensionToFids.erase(oldExt);
                            }
                        }
                    }
                }

                m_cache.erase(oldPath);
                qDebug() << "[Metadata] 检测到路径偏移，已从内存清理旧条目以防止重复计数:" << QString::fromStdWString(oldPath);
            }
        }
    }
    
    ensureActivated(nPath); 

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache[nPath].isTrash != isTrash) {
            m_cache[nPath].isTrash = isTrash;
            if (isTrash && !origPath.empty()) m_cache[nPath].originalPath = origPath;
            changed = true;
            isManaged = m_cache[nPath].isManaged;
            isInvalid = m_cache[nPath].isInvalid;
        }
        if (!fid.empty()) m_fidToPath[fid] = nPath;
    }
    
    if (changed) {
        // 2026-06-xx 按照用户要求：移入回收站时，必须和其他分类彻底隔离
        if (isTrash && !fid.empty()) {
            // 将文件移入“回收站”桶位（ID -8），这会自动解除所有现有分类关联
            CategoryRepo::moveToTrashBatch({fid});
        }

        // 2026-07-xx 架构修正：移入回收站应视为从活跃池移除。
        // 核心红线：仅当项已登记且非失效状态时，才执行计数同步。
        if (isManaged && !isInvalid) {
            CategoryRepo::incrementTotalFileCount(isTrash ? -1 : 1);
        }

        persistAsync(nPath);
        
        // 2026-06-xx 物理修复：状态变更后必须强制发射信号，驱动侧边栏重数一遍
        notifyUI(RefreshLevel::FullRebuild);
    }
}

void MetadataManager::setTrash(const std::wstring& path, bool isTrash) {
    std::wstring nPath = normalizePath(path);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it == m_cache.end()) return;

        // 2026-07-xx 按照规则同步活跃计数：仅对已登记项执行
        if (it->second.isManaged && it->second.isTrash != isTrash && !it->second.isInvalid) {
            CategoryRepo::incrementTotalFileCount(isTrash ? -1 : 1);
        }

        it->second.isTrash = isTrash;
        if (!isTrash) {
            it->second.originalPath = L""; // Clear on restore
        }
    }
    debouncePersist(nPath);
}

void MetadataManager::deletePermanently(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    // 2026-06-xx 逻辑优化：遵循“按需根除”原则。
    // 1. 首先检查内存缓存，判断该项目是否曾被记入数据库。
    std::string fid;
    bool existsInDb = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end()) {
            fid = it->second.fileId128;
            existsInDb = true;
        }
    }

    // 2. 物理加固：如果路径匹配失败（常见于 OS 将文件移入回收站后路径发生偏移），尝试通过物理 FID 反查
    if (!existsInDb) {
        if (fetchWinApiMetadataDirect(nPath, fid)) {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_fidToPath.find(fid);
            if (it != m_fidToPath.end()) {
                nPath = it->second; // 修正为缓存中的原始路径，确保 removeMetadataSync 能正确匹配
                existsInDb = true;
                qDebug() << "[Metadata] 路径匹配失败，已通过 FID 校准原始路径:" << QString::fromStdWString(nPath);
            }
        }
    }

    // 3. 如果项目从未被记入数据库，则无需执行任何数据库清理逻辑。
    if (!existsInDb) {
        qDebug() << "[Metadata] 永久删除项不在数据库中，跳过清理动作:" << QString::fromStdWString(nPath);
        notifyUI(RefreshLevel::FullRebuild);
        return;
    }
    
    // 4. 执行彻底根除。
    removeMetadataSync(nPath);

    // 5. 物理修复：发射全量刷新信号，确保侧边栏计数立即同步
    qDebug() << "[Metadata] 已执行永久删除清理，通知 UI 刷新:" << QString::fromStdWString(nPath);
    notifyUI(RefreshLevel::FullRebuild);
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { static_cast<wchar_t>(towupper(path[0])), L':', L'\\', L'\0' };
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial); return buf;
    }
    return L"UNKNOWN";
}

bool MetadataManager::fetchWinApiMetadataDirect(const std::wstring& path, std::string& outId128, std::wstring* outFrn, long long* outSize, std::wstring* outType, long long* outCtime, long long* outMtime, long long* outAtime) {
    HANDLE hFile = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    std::wstring vol = getVolumeSerialNumber(path);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outFrn) *outFrn = MetadataManager::generateDeterministicFrn(path);
        outId128 = MetadataManager::generateDeterministicSha256Id(path);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION basicInfo;
    if (GetFileInformationByHandle(hFile, &basicInfo)) {
        wchar_t frnBuf[17];
        unsigned long long fullFrn = (static_cast<unsigned long long>(basicInfo.nFileIndexHigh) << 32) | basicInfo.nFileIndexLow;
        swprintf(frnBuf, 17, L"%016llX", fullFrn);
        if (outFrn) *outFrn = frnBuf;
        outId128 = MetadataManager::generateFallbackFid(vol, frnBuf);
        if (outSize) *outSize = (static_cast<long long>(basicInfo.nFileSizeHigh) << 32) | basicInfo.nFileSizeLow;
        if (outType) *outType = (basicInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"folder" : L"file";
        auto toMS = [](const FILETIME& ft) {
            ULARGE_INTEGER ull; ull.LowPart = ft.dwLowDateTime; ull.HighPart = ft.dwHighDateTime;
            return static_cast<long long>((ull.QuadPart - 116444736000000000ULL) / 10000ULL);
        };
        if (outCtime) *outCtime = toMS(basicInfo.ftCreationTime);
        if (outMtime) *outMtime = toMS(basicInfo.ftLastWriteTime);
        if (outAtime) *outAtime = toMS(basicInfo.ftLastAccessTime);
        CloseHandle(hFile);
        return true;
    }
    CloseHandle(hFile);
    return false;
}

void MetadataManager::syncPhysicalMetadata(const std::wstring& path, bool notify) { persistAsync(path, notify); }

void MetadataManager::activateItem(const std::wstring& path) {
    instance().registerItem(path);
}

void MetadataManager::tryExtractDimensions(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    QFileInfo info(QString::fromStdWString(nPath));
    if (!info.isFile()) return;

    int w = 0, h = 0;
    
    // 2026-07-xx 按照计划：SVG 需特殊处理，防止 QImageReader 获取到错误的默认尺寸
    if (info.suffix().toLower() == "svg") {
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            w = sz.width();
            h = sz.height();
        }
    } else {
        // 方案 A: 尝试通过 QImageReader 获取尺寸 (仅读取头部，极快)
        QImageReader reader(info.absoluteFilePath());
        QSize sz = reader.size();
        if (sz.isValid()) {
            w = sz.width();
            h = sz.height();
        }
    }
    
    // 2026-07-xx 按照 Plan-29：如果 QImageReader 失败且是图像类型，尝试 Windows Shell 属性
#ifdef Q_OS_WIN
    if (w <= 0 || h <= 0) {
        // PSD/AI 等格式可能需要 Shell 接口
        // TODO: 后续可集成 IPropertyStore 进一步增强专业格式识别
    }
#endif

    if (w > 0 && h > 0) {
        std::unique_lock<std::shared_mutex> lock(instance().m_mutex);
        RuntimeMeta& meta = instance().m_cache[nPath];
        meta.width = w;
        meta.height = h;
    }
}

void MetadataManager::tryExtractColor(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    // 2026-07-xx 按照 Plan-29：在提取颜色时同步校准尺寸
    int currentW = 0, currentH = 0;
    {
        std::shared_lock<std::shared_mutex> lock(instance().m_mutex);
        const auto& m = instance().m_cache[nPath];
        currentW = m.width;
        currentH = m.height;
        if (!m.color.empty() && currentW > 0) return; 
    }
    
    QFileInfo info(QString::fromStdWString(nPath));
    QString qPath = QString::fromStdWString(nPath);
    bool success = false;
    
    if (info.isFile()) {
        if (ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower())) {
            // 2026-07-xx 按照建议：统一使用 getImageForAnalysis 以确保 SVG 正确栅格化
            QImage img = ArcMeta::UiHelper::getImageForAnalysis(qPath, 256);
            
            if (!img.isNull()) {
                // 如果之前没拿到尺寸，这里补救
                if (currentW <= 0) {
                    std::unique_lock<std::shared_mutex> lock(instance().m_mutex);
                    instance().m_cache[nPath].width = img.width();
                    instance().m_cache[nPath].height = img.height();
                }

                auto palette = ArcMeta::UiHelper::extractPalette(qPath);
                if (!palette.isEmpty()) {
                    QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                    instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), palette, false);
                    success = true;
                }
            }
        } else {
            // 2026-11-xx 按照 Plan-3：非图像文件直接晋升
            instance().setItemVisualMetadata(nPath, L"", {}, false);
            success = true;
        }
    } else if (info.isDir()) {
        QDir subDir(qPath);
        // 2026-07-xx 按照计划：扫描前 10 个图像文件并执行多样本一致性校验
        QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        
        struct Sample { QColor dominant; QVector<QPair<QColor, float>> palette; };
        QVector<Sample> samples;

        for (const auto& sf : subFiles) {
            if (ArcMeta::UiHelper::isGraphicsFile(sf.suffix().toLower())) {
                auto palette = ArcMeta::UiHelper::extractPalette(sf.absoluteFilePath());
                if (!palette.isEmpty()) {
                    samples.append({palette.first().first, palette});
                }
                if (samples.size() >= 10) break;
            }
        }

        if (!samples.isEmpty()) {
            int bestIdx = 0;
            int maxVotes = 0;
            for (int i = 0; i < samples.size(); ++i) {
                int votes = 0;
                for (int j = 0; j < samples.size(); ++j) {
                    if (ArcMeta::UiHelper::calculateDeltaE(samples[i].dominant, samples[j].dominant) < 20.0) {
                        votes++;
                    }
                }
                if (votes > maxVotes) {
                    maxVotes = votes;
                    bestIdx = i;
                }
            }

            // 聚合决策：若只有一个样本直接采纳；若多个样本，最强簇必须占据 30% 以上权重且至少有 2 个成员
            if (samples.size() == 1 || (maxVotes >= 2 && maxVotes >= samples.size() * 0.3)) {
                QColor dominant = ArcMeta::UiHelper::quantizeColor(samples[bestIdx].dominant);
                instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), samples[bestIdx].palette, false);
                success = true;
            }
        } else {
            // 2026-11-xx 按照 Plan-3：文件夹内无图像，直接晋升
            instance().setItemVisualMetadata(nPath, L"", {}, false);
            success = true;
        }
    }

    // 2026-07-xx 按照建议：解析失败（且属于图像类型）时加入重试队列
    if (!success && (info.isDir() || ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower()))) {
        {
            std::unique_lock<std::shared_mutex> lock(instance().m_mutex);
            // 查重：避免队列膨胀
            bool found = false;
            for(const auto& p : instance().m_visualRetryQueue) if(p == nPath) { found = true; break; }
            if (!found) {
                instance().m_visualRetryQueue.push_back(nPath);
                // 2026-07-xx 物理对齐：QTimer 非线程安全，必须通过元对象系统跨线程启动
                QMetaObject::invokeMethod(instance().m_retryTimer, "start", Qt::QueuedConnection);
            }
        }
    }
}

void MetadataManager::processVisualRetryQueue() {
    std::vector<std::wstring> batch;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_visualRetryQueue.empty()) {
            m_retryTimer->stop();
            return;
        }
        // 每次处理 5 个，防止阻塞
        size_t count = std::min(m_visualRetryQueue.size(), (size_t)5);
        for(size_t i = 0; i < count; ++i) batch.push_back(m_visualRetryQueue[i]);
    }

    // 异步执行，不阻塞 UI
    (void)QtConcurrent::run([this, batch]() {
        #ifdef Q_OS_WIN
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        #endif

        std::vector<std::wstring> finished;
        for (const auto& path : batch) {
            QFileInfo info(QString::fromStdWString(path));
            QString qPath = QString::fromStdWString(path);
            bool ok = false;

            if (info.isFile()) {
                if (ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower())) {
                    auto palette = ArcMeta::UiHelper::extractPalette(qPath);
                    if (!palette.isEmpty()) {
                        QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                        setItemVisualMetadata(path, dominant.name().toUpper().toStdWString(), palette, true);
                        ok = true;
                    }
                } else {
                    setItemVisualMetadata(path, L"", {}, true);
                    ok = true;
                }
            } else if (info.isDir()) {
                QDir subDir(qPath);
                QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                
                struct Sample { QColor dominant; QVector<QPair<QColor, float>> palette; };
                QVector<Sample> samples;

                for (const auto& sf : subFiles) {
                    if (ArcMeta::UiHelper::isGraphicsFile(sf.suffix().toLower())) {
                        auto palette = ArcMeta::UiHelper::extractPalette(sf.absoluteFilePath());
                        if (!palette.isEmpty()) {
                            samples.append({palette.first().first, palette});
                        }
                        if (samples.size() >= 10) break;
                    }
                }

                if (!samples.isEmpty()) {
                    int bestIdx = 0;
                    int maxVotes = 0;
                    for (int i = 0; i < samples.size(); ++i) {
                        int votes = 0;
                        for (int j = 0; j < samples.size(); ++j) {
                            if (ArcMeta::UiHelper::calculateDeltaE(samples[i].dominant, samples[j].dominant) < 20.0) {
                                votes++;
                            }
                        }
                        if (votes > maxVotes) {
                            maxVotes = votes;
                            bestIdx = i;
                        }
                    }

                    if (samples.size() == 1 || (maxVotes >= 2 && maxVotes >= samples.size() * 0.3)) {
                        QColor dominant = ArcMeta::UiHelper::quantizeColor(samples[bestIdx].dominant);
                        setItemVisualMetadata(path, dominant.name().toUpper().toStdWString(), samples[bestIdx].palette, true);
                        ok = true;
                    }
                } else {
                    setItemVisualMetadata(path, L"", {}, true);
                    ok = true;
                }
            }
            
            // 2026-07-xx 按照 Plan-28：重构移除策略
            // 只有成功，或者确定不是图像文件（无法提取）时，才从队列移除
            bool isGraphics = ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower());
            if (ok || (!isGraphics && !info.isDir())) {
                finished.push_back(path);
            }
        }

        // 从队列中移除已处理项
        if (!finished.empty()) {
            QMetaObject::invokeMethod(this, [this, finished]() {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                for (const auto& p : finished) {
                    auto it = std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), p);
                    if (it != m_visualRetryQueue.end()) m_visualRetryQueue.erase(it);
                }
                if (!m_visualRetryQueue.empty()) m_retryTimer->start();
            });
        }

        #ifdef Q_OS_WIN
        CoUninitialize();
        #endif
    });
}

void MetadataManager::registerArcmetaFrn(const std::wstring&) {
}

std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = MetadataManager::generateDeterministicSha256Id(path);
    return fid;
}

bool MetadataManager::moveMetadataToVolume(const std::wstring& path, const std::wstring& targetPath, const std::wstring& srcVol, const std::wstring& dstVol) {
    if (srcVol == dstVol) return false;

    std::wstring nSrcPath = normalizePath(path);
    std::wstring nDstPath = normalizePath(targetPath);

    RuntimeMeta rMeta = getMeta(nSrcPath);
    if (rMeta.fileId128.empty()) return false;

    sqlite3* srcDb = DatabaseManager::instance().getMemoryDb(srcVol);
    sqlite3* dstDb = DatabaseManager::instance().getMemoryDb(dstVol);
    if (!srcDb || !dstDb) return false;

    // 1. 从源库读取全量记录 (此处已由 rMeta 载入内存，直接写入目标库)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nDstPath] = rMeta;
        m_cache[nDstPath].isManaged = true; // 目标库写入即受控
        m_fidToPath[rMeta.fileId128] = nDstPath;
        // 清理旧路径关联
        if (m_cache.count(nSrcPath)) m_cache.erase(nSrcPath);
    }

    // 2. 写入目标库
    persistAsync(nDstPath, false);

    // 3. 从源库删除
    sqlite3_stmt* delStmt;
    if (sqlite3_prepare_v2(srcDb, "DELETE FROM metadata WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(delStmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    // 4. 同步分类关联 (跨库后 category_items 也需要迁移)
    sqlite3_stmt* catStmt;
    if (sqlite3_prepare_v2(srcDb, "SELECT category_id, path_hint, added_at FROM category_items WHERE file_id = ?", -1, &catStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(catStmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(catStmt) == SQLITE_ROW) {
            int catId = sqlite3_column_int(catStmt, 0);
            double addedAt = sqlite3_column_double(catStmt, 2);

            sqlite3_stmt* insStmt;
            if (sqlite3_prepare_v2(dstDb, "INSERT OR REPLACE INTO category_items (category_id, file_id, path_hint, added_at) VALUES (?, ?, ?, ?)", -1, &insStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(insStmt, 1, catId);
                sqlite3_bind_text(insStmt, 2, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(insStmt, 3, nDstPath.c_str(), -1, SQLITE_TRANSIENT); // 更新 path_hint 为新路径
                sqlite3_bind_double(insStmt, 4, addedAt);
                sqlite3_step(insStmt);
                sqlite3_finalize(insStmt);
            }
        }
        sqlite3_finalize(catStmt);
    }

    // 从源库删除关联
    if (sqlite3_prepare_v2(srcDb, "DELETE FROM category_items WHERE file_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(delStmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(delStmt);
        sqlite3_finalize(delStmt);
    }

    notifyUI(RefreshLevel::FullRebuild);
    return true;
}

void MetadataManager::persistAsync(const std::wstring& path, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    RuntimeMeta rMeta = getMeta(nPath);
    sqlite3* db = nullptr;
    
    // 2026-06-xx 架构重定向：判定是否为物理磁盘根目录（如 C:\）。
    // 理由：盘符置顶等元数据属于全应用级全局元数据，必须存入全局库以解决物理分库未挂载或盘符漂移冲突。
    if (nPath.length() == 3 && nPath[1] == L':' && (nPath[2] == L'\\' || nPath[2] == L'/')) {
        db = DatabaseManager::instance().getGlobalDb();
    } else {
        std::wstring volSerial = getVolumeSerialNumber(nPath);
        QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : "";
        db = DatabaseManager::instance().getMemoryDb(volSerial, letter);
    }
    if (!db) return;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO metadata (file_id, path, is_folder, rating, color, tags, note, url, ctime, mtime, atime, file_size, palettes, is_trash, original_path, is_invalid, width, height, ingestion_status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    bool isNew = true;
    {
        sqlite3_stmt* checkStmt;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM metadata WHERE file_id = ?", -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) isNew = false;
            sqlite3_finalize(checkStmt);
        }
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 2, nPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, rMeta.isFolder ? 1 : 0);
        sqlite3_bind_int(stmt, 4, rMeta.rating);
        sqlite3_bind_text16(stmt, 5, rMeta.color.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 6, rMeta.tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 7, rMeta.note.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 8, rMeta.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 9, rMeta.ctime);
        sqlite3_bind_int64(stmt, 10, rMeta.mtime);
        sqlite3_bind_int64(stmt, 11, rMeta.atime);
        sqlite3_bind_int64(stmt, 12, rMeta.fileSize);

        QJsonArray arr;
        for (const auto& pe : rMeta.palettes) {
            QJsonObject obj;
            obj["color"] = pe.color.name();
            obj["ratio"] = (double)pe.ratio;
            arr.append(obj);
        }
        QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        sqlite3_bind_blob(stmt, 13, ba.constData(), ba.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 14, rMeta.isTrash ? 1 : 0);
        sqlite3_bind_text16(stmt, 15, rMeta.originalPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 16, rMeta.isInvalid ? 1 : 0);
        sqlite3_bind_int(stmt, 17, rMeta.width);
        sqlite3_bind_int(stmt, 18, rMeta.height);
        sqlite3_bind_int(stmt, 19, rMeta.ingestionStatus);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            if (isNew) {
                // 2026-07-xx 物理修复：新项目入库时，若其状态为回收站或失效，则不增加总计数
                if (!rMeta.isFolder && !rMeta.isInvalid && !rMeta.isTrash) {
                    CategoryRepo::incrementTotalFileCount(1);
                }
            }
            // 2026-07-xx 物理同步：只要 SQL 执行成功，即确保内存标记为已登记，不再区分 isNew
            // 注意：此处不再调用 setManaged 以避免无限递归 debouncePersist
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache[nPath].isManaged = true;
            }
        }
        sqlite3_finalize(stmt);
    }
        
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}


void MetadataManager::parsePathComponents(const std::wstring& normalizedPath, bool isFolder, std::wstring& outName, std::wstring& outExt) {
    size_t lastSlash = normalizedPath.find_last_of(L"\\/");
    std::wstring fullName = (lastSlash == std::wstring::npos) ? normalizedPath : normalizedPath.substr(lastSlash + 1);

    if (isFolder) {
        outName = fullName;
        outExt = L"";
    } else {
        outName = fullName;
        size_t lastDot = fullName.find_last_of(L'.');
        if (lastDot != std::wstring::npos && lastDot > 0) {
            outExt = fullName.substr(lastDot + 1);
            // 统一转换为小写
            std::transform(outExt.begin(), outExt.end(), outExt.begin(), ::towlower);
        } else {
            outExt = L"";
        }
    }
}

std::wstring MetadataManager::getVolumeFromFid(const std::string& fid) {
    if (fid.empty()) return L"UNKNOWN";
    if (fid.find("FRN:") == 0) {
        size_t secondColon = fid.find(':', 4);
        if (secondColon != std::string::npos) {
            std::string vol = fid.substr(4, secondColon - 4);
            return QString::fromStdString(vol).toStdWString();
        }
    }
    return L"UNKNOWN";
}

void MetadataManager::unloadVolumeNameCache(const std::wstring& volSerial) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::string prefix = "FRN:" + QString::fromStdWString(volSerial).toUpper().toStdString() + ":";

    auto cleanupMap = [&](std::unordered_map<std::wstring, std::vector<std::string>>& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            auto& fids = it->second;
            fids.erase(std::remove_if(fids.begin(), fids.end(), [&](const std::string& fid) {
                return fid.find(prefix) == 0;
            }), fids.end());

            if (fids.empty()) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };

    cleanupMap(m_fileNameToFids);
    cleanupMap(m_folderNameToFids);
    cleanupMap(m_extensionToFids);
}

void MetadataManager::loadVolumeNameCache(const std::wstring& volSerial) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::string prefix = "FRN:" + QString::fromStdWString(volSerial).toUpper().toStdString() + ":";

    for (const auto& pair : m_cache) {
        const std::wstring& path = pair.first;
        const RuntimeMeta& meta = pair.second;
        if (meta.fileId128.find(prefix) == 0) {
            std::wstring name, ext;
            parsePathComponents(path, meta.isFolder, name, ext);
            if (!name.empty()) {
                if (meta.isFolder) {
                    auto& v = m_folderNameToFids[name];
                    if (std::find(v.begin(), v.end(), meta.fileId128) == v.end()) v.push_back(meta.fileId128);
                } else {
                    auto& v = m_fileNameToFids[name];
                    if (std::find(v.begin(), v.end(), meta.fileId128) == v.end()) v.push_back(meta.fileId128);
                    if (!ext.empty()) {
                        auto& ve = m_extensionToFids[ext];
                        if (std::find(ve.begin(), ve.end(), meta.fileId128) == ve.end()) ve.push_back(meta.fileId128);
                    }
                }
            }
        }
    }
}

std::vector<std::string> MetadataManager::getFileFidsByName(const std::wstring& filename) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
    auto it = m_fileNameToFids.find(lowerName);
    return (it != m_fileNameToFids.end()) ? it->second : std::vector<std::string>();
}

std::vector<std::string> MetadataManager::getFolderFidsByName(const std::wstring& foldername) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerName = foldername;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
    auto it = m_folderNameToFids.find(lowerName);
    return (it != m_folderNameToFids.end()) ? it->second : std::vector<std::string>();
}

std::vector<std::string> MetadataManager::getFidsByExtension(const std::wstring& extension) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerExt = extension;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::towlower);
    auto it = m_extensionToFids.find(lowerExt);
    return (it != m_extensionToFids.end()) ? it->second : std::vector<std::string>();
}

bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList&) {}
void MetadataManager::addToSyncLog(const std::wstring&) {}

QStringList MetadataManager::searchInCache(const QString& keyword, const QString& scopeSource, int categoryId, const QString& parentPath) {
    QStringList results; if (keyword.isEmpty()) return results;
    
    // 2026-07-xx 按照方案计划：实现范围感知搜索
    std::unordered_set<std::string> scopeFids;
    bool hasScope = false;

    if (scopeSource == "category" && categoryId != 0) {
        // 1. 分类范围搜索：获取该分类及其子分类下的所有 FID
        // 2026-07-xx 按照 Plan-81：支持递归搜索
        std::vector<int> targetIds = { categoryId };
        if (categoryId > 0) {
            targetIds = CategoryRepo::getSubtreeIds(categoryId);
        }
        auto items = CategoryRepo::getItemsInCategories(targetIds);
        for (const auto& item : items) scopeFids.insert(item.fileId128);
        hasScope = true;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    // 2026-07-xx 物理对账：规范化父路径前缀用于导航范围搜索
    std::wstring wParentPath = (scopeSource == "nav" && !parentPath.isEmpty()) ? normalizePath(parentPath.toStdWString()) : L"";
    if (!wParentPath.empty()) {
        bool endsWithSlash = false;
        if (!wParentPath.empty()) {
            wchar_t lastChar = wParentPath.back();
            if (lastChar == L'\\' || lastChar == L'/') endsWithSlash = true;
        }
        if (!endsWithSlash) {
            wParentPath += L'\\';
        }
    }

    for (std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.begin(); it != m_cache.end(); ++it) {
        const std::wstring& path = it->first; 
        const RuntimeMeta& meta = it->second;

        // 范围检查 (Scope Check)
        if (hasScope) {
            if (scopeFids.find(meta.fileId128) == scopeFids.end()) continue;
        } else if (!wParentPath.empty()) {
            // 物理视图下的路径范围匹配
            if (path.find(wParentPath) != 0) continue;
        }

        QString qPath = QString::fromStdWString(path); 
        QString qNote = QString::fromStdWString(meta.note);
        
        bool match = qPath.contains(keyword, Qt::CaseInsensitive) || qNote.contains(keyword, Qt::CaseInsensitive);
        if (!match) { 
            for (int i = 0; i < meta.tags.size(); ++i) { 
                if (meta.tags[i].contains(keyword, Qt::CaseInsensitive)) { match = true; break; } 
            } 
        }
        if (match) results << qPath;
    }
    return results;
}

QMap<QString, int> MetadataManager::getAllTags() const {
    QMap<QString, int> tagCounts;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->second.isManaged && !it->second.isInvalid && !it->second.isTrash) {
            for (const QString& tag : it->second.tags) {
                tagCounts[tag]++;
            }
        }
    }
    return tagCounts;
}

QList<QPair<QString, int>> MetadataManager::getTopTags(int limit) const {
    QMap<QString, int> counts = getAllTags();
    QList<QPair<QString, int>> list;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        list.append({it.key(), it.value()});
    }

    std::sort(list.begin(), list.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    if (list.size() > limit) {
        return list.mid(0, limit);
    }
    return list;
}

} // namespace ArcMeta
