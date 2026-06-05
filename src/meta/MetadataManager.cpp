#include <QFileInfo>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QThreadPool>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MetadataManager.h"
#include "MetadataDefs.h"
#include "AmMetaScch.h"
#include "AllFrnManager.h"
#include "DriverRepo.h"
#include "../mft/MftReader.h"
#include "../meta/CategoryRepo.h"

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

// --- Internal Helper Functions ---

static std::wstring normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path)));
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    if (qp.length() >= 2 && qp[1] == ':') qp[0] = qp[0].toUpper();
    return qp.toStdWString();
}

static std::string generateFallbackFid(const std::wstring& vol, const std::wstring& frn) {
    if (vol.empty() || frn.empty()) return "";
    return "FRN:" + QString::fromStdWString(vol).toUpper().toStdString() + ":" + QString::fromStdWString(frn).toUpper().toStdString();
}

static std::string generateDeterministicSha256Id(const std::wstring& path) {
    if (path.empty()) return "";
    std::wstring nPath = normalizePath(path);
    std::wstring vol = MetadataManager::getVolumeSerialNumber(nPath);
    QByteArray seed = QString::fromStdWString(vol + L":" + nPath).toUtf8();
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    return "PATHURL:" + hash.left(16).toHex().toUpper().toStdString();
}

static std::wstring generateDeterministicFrn(const std::wstring& path) {
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
            for (std::unordered_set<std::wstring>::const_iterator it = m_dirtyPaths.begin(); it != m_dirtyPaths.end(); ++it) {
                paths.push_back(*it);
            }
            m_dirtyPaths.clear();
        }
        for (size_t i = 0; i < paths.size(); ++i) persistAsync(paths[i]);
    });
}


void MetadataManager::initFromScchMode() {
    // 2026-06-xx 物理加固：防止重复初始化导致的 IO 风暴
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_loaded) {
            qDebug() << "[PERF] SCCH 缓存已在内存中，跳过重复加载";
            return;
        }
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    qDebug() << "[PERF] 开始加载分布式 SCCH 缓存...";
    loadDriverMetadata();
    std::unordered_map<std::wstring, RuntimeMeta> tempCache;
    std::unordered_map<std::string, std::wstring> tempFidToPath;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        tempCache = m_cache;
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (!it->second.fileId128.empty()) tempFidToPath[it->second.fileId128] = it->first;
        }
    }

    QMap<QString, QString> frnsMap = AllFrnManager::getAllFrns();
    
    for (QMap<QString, QString>::const_iterator itMap = frnsMap.constBegin(); itMap != frnsMap.constEnd(); ++itMap) {
        QString frnStr = itMap.key();
        QString lastKnownPath = itMap.value();
        std::wstring resolvedPath = QDir::toNativeSeparators(lastKnownPath).toStdWString();
        
        if (!QDir(QString::fromStdWString(resolvedPath)).exists()) {
            bool ok = false;
            unsigned long long frnVal = frnStr.toULongLong(&ok, 16);
            if (ok) {
                for (size_t d = 0; d < 26; ++d) {
                    std::wstring p = MftReader::instance().getPathFast(static_cast<int>(d), frnVal);
                    if (!p.empty()) {
                        if (p.find(L"metadata.scch") != std::wstring::npos) {
                            resolvedPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(p)).absolutePath()).toStdWString();
                        } else {
                            resolvedPath = p;
                        }
                        AllFrnManager::registerFrn(frnStr.toStdWString(), resolvedPath);
                        break;
                    }
                }
            }
        }
        
        ArcMeta::AmMetaScch loader(resolvedPath);
        if (loader.load()) {
            std::wstring nResolvedPath = normalizePath(resolvedPath);
            long long fSize = 0, fCtime = 0, fMtime = 0, fAtime = 0;
            std::string fId128;
            fetchWinApiMetadataDirect(resolvedPath, fId128, nullptr, &fSize, nullptr, &fCtime, &fMtime, &fAtime);

            const FolderMeta& f = loader.folder();
            RuntimeMeta fMeta;
            fMeta.rating = f.rating; fMeta.color = f.color;
            for (size_t i = 0; i < f.tags.size(); ++i) fMeta.tags << QString::fromStdWString(f.tags[i]);
            fMeta.pinned = f.pinned; fMeta.note = f.note; fMeta.url = f.url; fMeta.encrypted = f.encrypted;
            fMeta.isFolder = true;
            fMeta.fileId128 = fId128.empty() ? f.fileId128 : fId128;
            fMeta.fileSize = fSize; fMeta.ctime = fCtime; fMeta.mtime = fMtime; fMeta.atime = fAtime;
            fMeta.palettes = f.palettes;
            tempCache[nResolvedPath] = fMeta;

            const std::map<std::wstring, ItemMeta>& scchItems = loader.items();
            for (std::map<std::wstring, ItemMeta>::const_iterator itItem = scchItems.begin(); itItem != scchItems.end(); ++itItem) {
                const std::wstring& name = itItem->first;
                const ItemMeta& item = itItem->second;

                RuntimeMeta iMeta;
                iMeta.rating = item.rating; iMeta.color = item.color;
                for (size_t tIdx = 0; tIdx < item.tags.size(); ++tIdx) iMeta.tags << QString::fromStdWString(item.tags[tIdx]);
                iMeta.pinned = item.pinned; iMeta.note = item.note; iMeta.url = item.url; iMeta.encrypted = item.encrypted;
                iMeta.isFolder = (item.type == L"folder");
                iMeta.fileId128 = item.fileId128;
                
                std::wstring itemPath = resolvedPath + L"\\" + name;
                fetchWinApiMetadataDirect(itemPath, iMeta.fileId128, nullptr, &iMeta.fileSize, nullptr, &iMeta.ctime, &iMeta.mtime, &iMeta.atime);

                iMeta.palettes = item.palettes;
                std::wstring nItemPath = normalizePath(itemPath);
                tempCache[nItemPath] = iMeta;
                if (!iMeta.fileId128.empty()) tempFidToPath[iMeta.fileId128] = nItemPath;
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = tempCache;
        m_fidToPath = tempFidToPath;
        m_loaded = true;
    }
    qDebug() << "[PERF] 分布式 SCCH 缓存加载完成，项数:" << tempCache.size() << " 耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
    emit metaChanged("__RELOAD_ALL__");
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.find(nPath);
        if (it != m_cache.end()) return it->second;
    }

    QFileInfo info(QString::fromStdWString(nPath));
    std::wstring parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
    std::wstring fileName = info.fileName().toStdWString();

    ArcMeta::AmMetaScch loader(parentDir);
    if (loader.load()) {
        const std::map<std::wstring, ItemMeta>& its = loader.items();
        std::map<std::wstring, ItemMeta>::const_iterator it = its.find(fileName);
        if (it != its.end()) {
            const ItemMeta& item = it->second;
            RuntimeMeta rm;
            rm.rating = item.rating; rm.color = item.color;
            rm.pinned = item.pinned; rm.encrypted = item.encrypted;
            rm.note = item.note; rm.url = item.url; rm.palettes = item.palettes;
            rm.isFolder = (item.type == L"folder");
            rm.fileId128 = item.fileId128;
            fetchWinApiMetadataDirect(nPath, rm.fileId128, nullptr, &rm.fileSize, nullptr, &rm.ctime, &rm.mtime, &rm.atime);
            for (size_t i = 0; i < item.tags.size(); ++i) rm.tags << QString::fromStdWString(item.tags[i]);
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath] = rm;
            if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
            return rm;
        }
        if (info.isDir()) {
            const FolderMeta& folder = loader.folder();
            if (!folder.isDefault()) {
                RuntimeMeta rm;
                rm.rating = folder.rating; rm.color = folder.color;
                rm.pinned = folder.pinned; rm.note = folder.note; rm.url = folder.url; rm.palettes = folder.palettes;
                rm.isFolder = true;
                rm.fileId128 = folder.fileId128;
                fetchWinApiMetadataDirect(nPath, rm.fileId128, nullptr, &rm.fileSize, nullptr, &rm.ctime, &rm.mtime, &rm.atime);
                for (size_t i = 0; i < folder.tags.size(); ++i) rm.tags << QString::fromStdWString(folder.tags[i]);
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache[nPath] = rm;
                if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
                return rm;
            }
        }
    }
    return RuntimeMeta();
}

std::wstring MetadataManager::getPathByFid(const std::string& fid) {
    if (fid.empty()) return L"";
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_fidToPath.find(fid);
    return (it != m_fidToPath.end()) ? it->second : L"";
}

void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].rating = rating; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].color = color; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].pinned = pinned; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].tags = tags; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].note = note; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].url = url; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted, bool notify) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].encrypted = encrypted; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = normalizePath(path);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].palettes = entries; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = normalizePath(path);
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
    std::wstring nPath = normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end() && !it->second.palettes.empty()) {
            QVector<QColor> colors;
            for (const auto& entry : it->second.palettes) colors << entry.color;
            return colors;
        }
    }

    QFileInfo info(QString::fromStdWString(nPath));
    ArcMeta::AmMetaScch loader(QDir::toNativeSeparators(info.absolutePath()).toStdWString());
    if (loader.load()) {
        std::vector<PaletteEntry> entries;
        if (info.isDir()) entries = loader.folder().palettes;
        else {
            const std::map<std::wstring, ItemMeta>& its = loader.items();
            if (its.count(info.fileName().toStdWString())) entries = its.at(info.fileName().toStdWString()).palettes;
        }

        if (!entries.empty()) {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath].palettes = entries;
        }

        QVector<QColor> colors;
        for (size_t i = 0; i < entries.size(); ++i) colors << entries[i].color;
        return colors;
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
        }
    }
    emit metaChanged(QString::fromStdWString(newPath));
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (std::unordered_map<std::wstring, RuntimeMeta>::iterator it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == path || it->first.find(path + L"\\") == 0 || it->first.find(path + L"/") == 0) {
                if (!it->second.fileId128.empty()) m_fidToPath.erase(it->second.fileId128);
                it = m_cache.erase(it);
            }
            else ++it;
        }
    }
    QFileInfo info(QString::fromStdWString(path));
    if (info.isDir()) QFile::remove(info.absoluteFilePath() + "/metadata.scch");
    else {
        ArcMeta::AmMetaScch scchLoader(info.absolutePath().toStdWString());
        if (scchLoader.load()) { scchLoader.remove(info.fileName().toStdWString()); scchLoader.save(); }
    }
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
        if (outFrn) *outFrn = generateDeterministicFrn(path);
        outId128 = generateDeterministicSha256Id(path);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION basicInfo;
    if (GetFileInformationByHandle(hFile, &basicInfo)) {
        wchar_t frnBuf[17];
        unsigned long long fullFrn = (static_cast<unsigned long long>(basicInfo.nFileIndexHigh) << 32) | basicInfo.nFileIndexLow;
        swprintf(frnBuf, 17, L"%016llX", fullFrn);
        if (outFrn) *outFrn = frnBuf;
        outId128 = generateFallbackFid(vol, frnBuf);
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

void MetadataManager::syncPhysicalMetadata(const std::wstring& path) { persistAsync(path); }

std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = generateDeterministicSha256Id(path);
    return fid;
}

void MetadataManager::persistAsync(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    QFileInfo info(QString::fromStdWString(nPath));
    std::wstring parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
    std::wstring fileName = info.fileName().toStdWString();
    RuntimeMeta rMeta = getMeta(nPath);

    if (info.isDir() && info.isRoot()) {
        DriverEntry de;
        de.volumePath = nPath;
        de.rating = rMeta.rating;
        de.color = rMeta.color;
        de.pinned = rMeta.pinned;
        de.note = rMeta.note;
        de.url = rMeta.url;
        for (int i = 0; i < rMeta.tags.size(); ++i) de.tags.push_back(rMeta.tags[i].toStdWString());
        de.palettes = rMeta.palettes;
        DriverRepo::update(de);
    } else {
        ArcMeta::AmMetaScch loader(parentDir);
        loader.load();
        if (info.isDir()) {
            FolderMeta& folder = loader.folder();
            folder.rating = rMeta.rating; folder.color = rMeta.color;
            folder.pinned = rMeta.pinned; folder.note = rMeta.note;
            folder.url = rMeta.url;
            folder.tags.clear(); for (int i = 0; i < rMeta.tags.size(); ++i) folder.tags.push_back(rMeta.tags[i].toStdWString());
            folder.palettes = rMeta.palettes;
        } else {
            ItemMeta& item = loader.items()[fileName];
            item.rating = rMeta.rating; item.color = rMeta.color;
            item.pinned = rMeta.pinned; item.encrypted = rMeta.encrypted;
            item.note = rMeta.note; item.url = rMeta.url;
            item.tags.clear(); for (int i = 0; i < rMeta.tags.size(); ++i) item.tags.push_back(rMeta.tags[i].toStdWString());
            item.palettes = rMeta.palettes;
        }
        loader.save();
    }
    std::wstring metaPath = parentDir + L"\\metadata.scch";
    std::wstring fileFrn; std::string fileFid;
    if (fetchWinApiMetadataDirect(metaPath, fileFid, &fileFrn)) AllFrnManager::registerFrn(fileFrn, parentDir);
    emit metaChanged(QString::fromStdWString(nPath));
}

void MetadataManager::loadDriverMetadata() {
    qint64 start = QDateTime::currentMSecsSinceEpoch();
    std::vector<DriverEntry> drivers = DriverRepo::loadAll();
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& de : drivers) {
        std::wstring nPath = normalizePath(de.volumePath);
        RuntimeMeta rm;
        rm.rating = de.rating;
        rm.color = de.color;
        rm.pinned = de.pinned;
        rm.note = de.note;
        rm.url = de.url;
        for (const auto& t : de.tags) rm.tags << QString::fromStdWString(t);
        rm.palettes = de.palettes;
        rm.isFolder = true;
        
        fetchWinApiMetadataDirect(nPath, rm.fileId128, nullptr, &rm.fileSize, nullptr, &rm.ctime, &rm.mtime, &rm.atime);
        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
    }
    qDebug() << "[PERF] 驱动器根目录元数据加载耗时:" << (QDateTime::currentMSecsSinceEpoch() - start) << "ms";
}

bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList&) {}
void MetadataManager::addToSyncLog(const std::wstring&) {}
void MetadataManager::saveSyncLog() {}

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
