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

    // 扫描所有已加载的数据库
    // 2026-06-xx 逻辑加固：由于驱动器序列号在不同机器上可能重复或变化，
    // 我们必须确保启动时扫描 .arcmeta 目录下所有物理分库。
    QString metaDir = QCoreApplication::applicationDirPath() + "/.arcmeta";
    QDir dir(metaDir);
    if (dir.exists()) {
        // 2026-06-xx 物理修复：必须包含 Hidden 和 System 标志，因为 DatabaseManager 会将数据库设为隐藏属性
        QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files | QDir::Hidden | QDir::System);
        qDebug() << "[Metadata] 发现物理分库数量:" << dbFiles.size();
        for (const QString& dbFile : dbFiles) {
            // 文件名格式: Arcmeta_XXXX.db -> 提取 XXXX
            QString volSerial = dbFile.mid(8, dbFile.length() - 8 - 3);
            sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial.toStdWString());
            if (!db) continue;

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

                    rm.isManaged = true; // 2026-06-xx 物理同步：从数据库加载的项必然是已登记项
                    tempCache[path] = rm;
                    if (!rm.fileId128.empty()) tempFidToPath[rm.fileId128] = path;
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = tempCache;
        m_fidToPath = tempFidToPath;
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
            if (!path.isEmpty()) emit metaChanged(path);
            break;
        case RefreshLevel::FullRebuild:
            notifyFullUIRebuild();
            break;
    }
}

void MetadataManager::notifyCategoryCountChanged() {
    // 2026-06-xx 物理对账：在发射信号前，触发一遍同步计数，确保 UI 获取的是最新数据库数值
    CategoryRepo::fullRecount();
    emit metaChanged("__RELOAD_COUNT__");
}

void MetadataManager::notifyFullUIRebuild() {
    // 2026-06-xx 物理对账：全量重建前必须确保计数器已对齐
    CategoryRepo::fullRecount();
    emit metaChanged("__RELOAD_ALL__");
}

void MetadataManager::registerItem(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    // 1. 激活项目 (获取 FID/FRN 等物理属性)
    ensureActivated(nPath);
    // 2. 物理同步 (存入数据库)
    syncPhysicalMetadata(nPath, false);
    // 3. 视觉预热 (提取颜色)
    tryExtractColor(nPath);
    // 4. 通知 UI 刷新该路径
    notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
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
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_cache.find(nPath) != m_cache.end()) return;

    // 2026-06-xx 逻辑修复：点击/激活文件不应导致“全部数据”计数增加。
    // 计数应仅反映数据库中已持久化的项目总数。
    RuntimeMeta rm;
    std::wstring frn;
    std::wstring type;
    if (fetchWinApiMetadataDirect(nPath, rm.fileId128, &frn, &rm.fileSize, &type, &rm.ctime, &rm.mtime, &rm.atime)) {
        rm.isFolder = (type == L"folder");

        // 2026-06-xx 物理同步：首次激活项时，执行一次数据库存在性检查。
        // 如果该文件已在数据库中（不论属于哪个驱动器分库），标记为 isManaged，驱动 UI 绿对勾显示。
        std::wstring volSerial = getVolumeSerialNumber(nPath);
        sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
        if (db && !rm.fileId128.empty()) {
            sqlite3_stmt* checkStmt;
            if (sqlite3_prepare_v2(db, "SELECT 1 FROM metadata WHERE file_id = ?", -1, &checkStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(checkStmt, 1, rm.fileId128.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(checkStmt) == SQLITE_ROW) {
                    rm.isManaged = true;
                }
                sqlite3_finalize(checkStmt);
            }
        }

        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
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

void MetadataManager::setInvalid(const std::wstring& path, bool invalid, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    bool changed = false;
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        if (m_cache[nPath].isInvalid != invalid) {
            m_cache[nPath].isInvalid = invalid; 
            changed = true;
        }
    }
    
    if (changed) {
        // 如果标记为失效，活跃总数减少；如果恢复，活跃总数增加
        CategoryRepo::incrementTotalFileCount(invalid ? -1 : 1);
        if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
        debouncePersist(nPath);
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

void MetadataManager::renameItem(const std::wstring& oldPath, const std::wstring& newPath) {
    std::wstring nOld = normalizePath(oldPath);
    std::wstring nNew = normalizePath(newPath);
    if (nOld == nNew) return;

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nOld);
        if (it != m_cache.end()) { 
            std::string fid = it->second.fileId128;
            m_cache[nNew] = it->second; 
            m_cache.erase(it); 
            if (!fid.empty()) m_fidToPath[fid] = nNew;

            // 物理同步：更新 SQLite 路径
            std::wstring volSerial = getVolumeSerialNumber(nNew);
            sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
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
    notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nNew));
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring volSerial = getVolumeSerialNumber(nPath);
    sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
    
    int totalDelta = 0;
    std::vector<std::string> fids;
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == nPath || it->first.find(nPath + L"\\") == 0 || it->first.find(nPath + L"/") == 0) {
                // 2026-06-xx 物理对齐：回收站项也计入全部数据，因此删除时必须一并扣减
                if (!it->second.isFolder && !it->second.isInvalid) {
                    totalDelta--;
                }
                if (!it->second.fileId128.empty()) {
                    fids.push_back(it->second.fileId128);
                    m_fidToPath.erase(it->second.fileId128);
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
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        
        // 核心修复：防止内存中出现同一个 FID 的多条路径记录（物理偏移导致的重复计数）
        if (!fid.empty() && m_fidToPath.count(fid)) {
            std::wstring oldPath = m_fidToPath[fid];
            if (oldPath != nPath) {
                m_cache.erase(oldPath);
                qDebug() << "[Metadata] 检测到路径偏移，已从内存清理旧条目以防止重复计数:" << QString::fromStdWString(oldPath);
            }
        }

        // 2026-06-xx 物理加固：确保新路径条目已激活
        // 注意：此处必须先释放锁或在 ensureActivated 内部不重复加锁
    }
    
    ensureActivated(nPath); 

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache[nPath].isTrash != isTrash) {
            m_cache[nPath].isTrash = isTrash;
            if (isTrash && !origPath.empty()) m_cache[nPath].originalPath = origPath;
            changed = true;
        }
        if (!fid.empty()) m_fidToPath[fid] = nPath;
    }
    
    if (changed) {
        // 2026-06-xx 按照用户要求：移入回收站时，必须和其他分类彻底隔离
        if (isTrash && !fid.empty()) {
            // 将文件移入“回收站”桶位（ID -2），这会自动解除所有现有分类关联
            CategoryRepo::moveToTrashBatch({fid});
        }

        // 2026-06-xx 物理修正：移入回收站仅属于分类迁移，不应减少“全部数据”的计数。
        // 只有永久删除才会导致总数减少。
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

void MetadataManager::tryExtractColor(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    if (!instance().getMeta(nPath).color.empty()) return;
    
    QFileInfo info(QString::fromStdWString(nPath));
    QString qPath = QString::fromStdWString(nPath);
    
    if (info.isFile()) {
        if (ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower())) {
            auto palette = ArcMeta::UiHelper::extractPalette(qPath);
            if (!palette.isEmpty()) {
                QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), palette, false);
            }
        }
    } else if (info.isDir()) {
        QDir subDir(qPath);
        QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const auto& sf : subFiles) {
            if (ArcMeta::UiHelper::isGraphicsFile(sf.suffix().toLower())) {
                auto palette = ArcMeta::UiHelper::extractPalette(sf.absoluteFilePath());
                if (!palette.isEmpty()) {
                    QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                    instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), palette, false);
                    break;
                }
            }
        }
    }
}

void MetadataManager::registerArcmetaFrn(const std::wstring&) {
}

std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = MetadataManager::generateDeterministicSha256Id(path);
    return fid;
}

void MetadataManager::persistAsync(const std::wstring& path, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    RuntimeMeta rMeta = getMeta(nPath);
    std::wstring volSerial = getVolumeSerialNumber(nPath);
    sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
    if (!db) return;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO metadata (file_id, path, is_folder, rating, color, tags, note, url, ctime, mtime, atime, file_size, palettes, is_trash, original_path, is_invalid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
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

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            if (isNew) {
                if (!rMeta.isFolder) CategoryRepo::incrementTotalFileCount(1);
                // 物理同步：写入数据库后，内存立即标记为已登记，驱动打勾显示
                setManaged(nPath, true, false);
            }
        }
        sqlite3_finalize(stmt);
    }
        
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}


bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList&) {}
void MetadataManager::addToSyncLog(const std::wstring&) {}

QStringList MetadataManager::searchInCache(const QString& keyword) {
    QStringList results; if (keyword.isEmpty()) return results;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.begin(); it != m_cache.end(); ++it) {
        const std::wstring& path = it->first; const RuntimeMeta& meta = it->second;
        QString qPath = QString::fromStdWString(path); QString qNote = QString::fromStdWString(meta.note);
        bool match = qPath.contains(keyword, Qt::CaseInsensitive) || qNote.contains(keyword, Qt::CaseInsensitive);
        if (!match) { for (int i = 0; i < meta.tags.size(); ++i) { if (meta.tags[i].contains(keyword, Qt::CaseInsensitive)) { match = true; break; } } }
        if (match) results << qPath;
    }
    return results;
}

} // namespace ArcMeta
