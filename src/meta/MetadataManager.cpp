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
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path)));
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    if (qp.length() >= 2 && qp[1] == ':') qp[0] = qp[0].toUpper();
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
        QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files);
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
                    std::wstring path = wpath ? wpath : L"";

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

    // 2026-06-xx 物理对账：在镜像完全载入内存后，立即触发账本重计
    // 必须确保 fullRecount 在 m_loaded = true 之后执行，因为盘点需要读取缓存
    CategoryRepo::fullRecount();
    qDebug() << "[PERF] SQLite 元数据镜像构建完成。内存映射数:" << tempCache.size()
             << " ID索引数:" << tempFidToPath.size()
             << " 耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
    emit metaChanged("__RELOAD_ALL__");
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
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
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
        if (notify) emit metaChanged(QString::fromStdWString(nPath));
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
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].pinned = pinned; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nPath].tags = tags;
    }

    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].note = note; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].url = url; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].encrypted = encrypted; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].palettes = entries; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
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
    
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
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
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        std::unordered_map<std::wstring, RuntimeMeta>::iterator it = m_cache.find(oldPath);
        if (it != m_cache.end()) { 
            std::string fid = it->second.fileId128;
            m_cache[newPath] = it->second; 
            m_cache.erase(it); 
            if (!fid.empty()) m_fidToPath[fid] = newPath;

            // 物理同步：更新 SQLite 路径
            std::wstring volSerial = getVolumeSerialNumber(newPath);
            sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
            if (db) {
                sqlite3_stmt* stmt;
                const char* sql = "UPDATE metadata SET path = ? WHERE file_id = ?";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text16(stmt, 1, newPath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
    }
    emit metaChanged(QString::fromStdWString(newPath));
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring volSerial = getVolumeSerialNumber(nPath);
    sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial);
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (std::unordered_map<std::wstring, RuntimeMeta>::iterator it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == nPath || it->first.find(nPath + L"\\") == 0 || it->first.find(nPath + L"/") == 0) {
                // 只有非失效且非文件夹的文件删除时才扣减总计数
                // 因为失效数据已经从总数据中剥离统计了（或者说失效数据本身也是资产，用户手动删除时才真正减少）
                if (!it->second.isFolder && !it->second.isTrash && !it->second.isInvalid) {
                    CategoryRepo::incrementTotalFileCount(-1);
                }
                if (!it->second.fileId128.empty()) m_fidToPath.erase(it->second.fileId128);
                
                // 从 SQLite 中删除
                if (db) {
                    sqlite3_stmt* stmt;
                    const char* sql = "DELETE FROM metadata WHERE path = ? OR path LIKE ?";
                    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text16(stmt, 1, it->first.c_str(), -1, SQLITE_TRANSIENT);
                        std::wstring pattern = it->first + L"\\%";
                        sqlite3_bind_text16(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }

                it = m_cache.erase(it);
            }
            else ++it;
        }
    }
}

void MetadataManager::markAsTrash(const std::wstring& path, bool isTrash, const std::wstring& origPath) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    bool changed = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache[nPath].isTrash != isTrash) {
            m_cache[nPath].isTrash = isTrash;
            if (isTrash && !origPath.empty()) m_cache[nPath].originalPath = origPath;
            changed = true;
        }
    }

    if (changed) {
        // 如果进入回收站，活跃总数减少；如果还原，活跃总数增加
        CategoryRepo::incrementTotalFileCount(isTrash ? -1 : 1);
        persistAsync(nPath);
    }
}

void MetadataManager::deletePermanently(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::string fid = getFileIdSync(nPath);
    
    // 1. 从关联表彻底删除
    CategoryRepo::removeAllCategories(fid);
    
    // 2. 从物理元数据表彻底删除
    removeMetadataSync(nPath);
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { path[0], L':', L'\\', L'\0' };
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
    std::string fid; std::wstring frn;
    if (fetchWinApiMetadataDirect(path, fid, &frn)) {
        std::wstring nPath = MetadataManager::normalizePath(path);
        // 激活并持久化
        instance().ensureActivated(nPath);
        instance().syncPhysicalMetadata(nPath);
    }
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
            if (isNew && !rMeta.isFolder) {
                CategoryRepo::incrementTotalFileCount(1);
            }
        }
        sqlite3_finalize(stmt);
    }
        
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
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
