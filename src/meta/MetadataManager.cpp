#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
#include "../mft/MftReader.h"
#include "../db/CategoryRepo.h"

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

// --- 内部静态工具函数 ---

/**
 * @brief 标准化路径
 */
static std::wstring normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path)));
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    if (qp.length() >= 2 && qp[1] == ':') qp[0] = qp[0].toUpper();
    return qp.toStdWString();
}

/**
 * @brief 物理锚点 Fallback ID 生成
 */
static std::string generateFallbackFid(const std::wstring& vol, const std::wstring& frn) {
    if (vol.empty() || frn.empty()) return "";
    return "FRN:" + QString::fromStdWString(vol).toUpper().toStdString() + ":" + QString::fromStdWString(frn).toUpper().toStdString();
}

/**
 * @brief SHA-256 确定性 ID 生成
 */
static std::string generateDeterministicSha256Id(const std::wstring& path) {
    if (path.empty()) return "";
    std::wstring nPath = normalizePath(path);
    std::wstring vol = MetadataManager::getVolumeSerialNumber(nPath);
    QByteArray seed = QString::fromStdWString(vol + L":" + nPath).toUtf8();
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    return "PATHURL:" + hash.left(16).toHex().toUpper().toStdString();
}

/**
 * @brief SHA-256 确定性伪 FRN 生成
 */
static std::wstring generateDeterministicFrn(const std::wstring& path) {
    if (path.empty()) return L"VIRTUAL_EMPTY";
    QByteArray hash = QCryptographicHash::hash(QString::fromStdWString(path).toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.left(8).toHex().toUpper()).toStdWString();
}

// --- MetadataManager 实现 ---

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
            paths.assign(m_dirtyPaths.begin(), m_dirtyPaths.end());
            m_dirtyPaths.clear();
        }
        for (const auto& p : paths) persistAsync(p);
    });
}


void MetadataManager::initFromScchMode() {
    std::unordered_map<std::wstring, RuntimeMeta> tempCache;
    auto frnsMap = AllFrnManager::getAllFrns();
    
    for (auto it = frnsMap.begin(); it != frnsMap.end(); ++it) {
        QString frnStr = it.key();
        QString lastKnownPath = it.value();
        std::wstring resolvedPath = lastKnownPath.toStdWString();
        
        bool ok = false;
        uint64_t frnVal = frnStr.toULongLong(&ok, 16);
        if (ok) {
            for (size_t d = 0; d < 26; ++d) {
                std::wstring p = MftReader::instance().getPathFast(d, frnVal);
                if (!p.empty()) {
                    if (p.find(L"metadata.scch") != std::wstring::npos) {
                        resolvedPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(p)).absolutePath()).toStdWString();
                    } else {
                        resolvedPath = p;
                    }
                    break;
                }
            }
        }
        
        AmMetaScch amScch(resolvedPath);
        if (amScch.load()) {
            std::wstring nResolvedPath = normalizePath(resolvedPath);
            const auto& f = amScch.folder();
            RuntimeMeta fMeta;
            fMeta.rating = f.rating; fMeta.color = f.color;
            for (const auto& t : f.tags) fMeta.tags << QString::fromStdWString(t);
            fMeta.pinned = f.pinned; fMeta.note = f.note; fMeta.url = f.url; fMeta.encrypted = f.encrypted;
            fMeta.palettes = f.palettes;
            tempCache[nResolvedPath] = std::move(fMeta);

            for (const auto& [name, item] : amScch.items()) {
                RuntimeMeta iMeta;
                iMeta.rating = item.rating; iMeta.color = item.color;
                for (const auto& t : item.tags) iMeta.tags << QString::fromStdWString(t);
                iMeta.pinned = item.pinned; iMeta.note = item.note; iMeta.url = item.url; iMeta.encrypted = item.encrypted;
                iMeta.palettes = item.palettes;
                std::wstring itemPath = resolvedPath + L"\\" + name;
                tempCache[normalizePath(itemPath)] = std::move(iMeta);
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = std::move(tempCache);
    }
    emit metaChanged("__RELOAD_ALL__");
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end()) return it->second;
    }

    QFileInfo info(QString::fromStdWString(nPath));
    std::wstring parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
    std::wstring fileName = info.fileName().toStdWString();

    AmMetaScch amScch(parentDir);
    if (amScch.load()) {
        auto& items = amScch.items();
        if (items.count(fileName)) {
            const auto& item = items.at(fileName);
            RuntimeMeta rm;
            rm.rating = item.rating; rm.color = item.color;
            rm.pinned = item.pinned; rm.encrypted = item.encrypted;
            rm.note = item.note; rm.url = item.url; rm.palettes = item.palettes;
            for (const auto& t : item.tags) rm.tags << QString::fromStdWString(t);
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath] = rm;
            return rm;
        }
        if (info.isDir()) {
            const auto& folder = amScch.folder();
            if (!folder.isDefault()) {
                RuntimeMeta rm;
                rm.rating = folder.rating; rm.color = folder.color;
                rm.pinned = folder.pinned; rm.note = folder.note; rm.url = folder.url; rm.palettes = folder.palettes;
                for (const auto& t : folder.tags) rm.tags << QString::fromStdWString(t);
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_cache[nPath] = rm;
                return rm;
            }
        }
    }
    return RuntimeMeta();
}

void MetadataManager::setRating(const std::wstring& path, int rating) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].rating = rating; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].color = color; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].pinned = pinned; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].tags = tags; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].note = note; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].url = url; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted) {
    std::wstring nPath = normalizePath(path);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].encrypted = encrypted; }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes) {
    std::wstring nPath = normalizePath(path);
    std::vector<PaletteEntry> entries;
    for (const auto& p : palettes) entries.push_back({p.first, p.second});
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].palettes = entries; }
    QFileInfo info(QString::fromStdWString(nPath));
    AmMetaScch amScch(QDir::toNativeSeparators(info.absolutePath()).toStdWString());
    if (amScch.load()) {
        if (info.isDir()) amScch.folder().palettes = entries;
        else amScch.items()[info.fileName().toStdWString()].palettes = entries;
        amScch.save();
    }
    emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

QVector<QColor> MetadataManager::getPalettes(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    QFileInfo info(QString::fromStdWString(nPath));
    AmMetaScch amScch(QDir::toNativeSeparators(info.absolutePath()).toStdWString());
    if (amScch.load()) {
        std::vector<PaletteEntry> entries;
        if (info.isDir()) entries = amScch.folder().palettes;
        else {
            auto& items = amScch.items();
            if (items.count(info.fileName().toStdWString())) entries = items.at(info.fileName().toStdWString()).palettes;
        }
        QVector<QColor> colors;
        for (const auto& e : entries) colors << e.color;
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
        auto it = m_cache.find(oldPath);
        if (it != m_cache.end()) { m_cache[newPath] = std::move(it->second); m_cache.erase(it); }
    }
    emit metaChanged(QString::fromStdWString(newPath));
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == path || it->first.find(path + L"\\") == 0 || it->first.find(path + L"/") == 0) it = m_cache.erase(it);
            else ++it;
        }
    }
    QFileInfo info(QString::fromStdWString(path));
    if (info.isDir()) QFile::remove(info.absoluteFilePath() + "/metadata.scch");
    else {
        AmMetaScch scch(info.absolutePath().toStdWString());
        if (scch.load()) { scch.remove(info.fileName().toStdWString()); scch.save(); }
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
            return (long long)((ull.QuadPart - 116444736000000000ULL) / 10000ULL);
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
        QString driversPath = qApp->applicationDirPath() + "/FERREX_drivers.scch";
        QFile file(driversPath);
        QJsonObject root;
        if (file.open(QIODevice::ReadOnly)) { root = QJsonDocument::fromJson(file.readAll()).object(); file.close(); }
        QJsonObject driveMeta;
        driveMeta["rating"] = rMeta.rating;
        driveMeta["color"] = QString::fromStdWString(rMeta.color);
        driveMeta["pinned"] = rMeta.pinned;
        driveMeta["note"] = QString::fromStdWString(rMeta.note);
        driveMeta["url"] = QString::fromStdWString(rMeta.url);
        QJsonArray tagsArr; for (const auto& t : rMeta.tags) tagsArr.append(t);
        driveMeta["tags"] = tagsArr;
        root[QString::fromStdWString(nPath)] = driveMeta;
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(root).toJson()); file.close(); }
    } else {
        AmMetaScch amScch(parentDir);
        amScch.load();
        if (info.isDir()) {
            FolderMeta& folder = amScch.folder();
            folder.rating = rMeta.rating; folder.color = rMeta.color;
            folder.pinned = rMeta.pinned; folder.note = rMeta.note;
            folder.url = rMeta.url;
            folder.tags.clear(); for (const auto& t : rMeta.tags) folder.tags.push_back(t.toStdWString());
            folder.palettes = rMeta.palettes;
        } else {
            ItemMeta& item = amScch.items()[fileName];
            item.rating = rMeta.rating; item.color = rMeta.color;
            item.pinned = rMeta.pinned; item.encrypted = rMeta.encrypted;
            item.note = rMeta.note; item.url = rMeta.url;
            item.tags.clear(); for (const auto& t : rMeta.tags) item.tags.push_back(t.toStdWString());
            item.palettes = rMeta.palettes;
        }
        amScch.save();
    }
    std::wstring metaPath = parentDir + L"\\metadata.scch";
    std::wstring fileFrn; std::string fileFid;
    if (fetchWinApiMetadataDirect(metaPath, fileFid, &fileFrn)) AllFrnManager::registerFrn(fileFrn, parentDir);
    std::string metaFid;
    if (fetchWinApiMetadataDirect(metaPath, metaFid, nullptr)) addToSyncLog(QString::fromStdString(metaFid).toStdWString());
    else addToSyncLog(parentDir);
    emit metaChanged(QString::fromStdWString(nPath));
}

bool MetadataManager::hasPendingSync() const { return QFile::exists(qApp->applicationDirPath() + "/Synchronize.scch"); }
QStringList MetadataManager::getPendingSyncDirs() {
    QFile file(qApp->applicationDirPath() + "/Synchronize.scch");
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isArray()) return doc.toVariant().toStringList();
    return {};
}

void MetadataManager::removeFidsFromLog(const QStringList& fidsToRemove) {
    QString logPath = qApp->applicationDirPath() + "/Synchronize.scch";
    if (!QFile::exists(logPath)) return;
    QStringList current;
    { QFile file(logPath); if (file.open(QIODevice::ReadOnly)) current = QJsonDocument::fromJson(file.readAll()).toVariant().toStringList(); }
    bool changed = false;
    for (const auto& f : fidsToRemove) { if (current.contains(f)) { current.removeAll(f); changed = true; } }
    if (changed) {
        if (current.isEmpty()) { QFile::remove(logPath); emit pendingSyncChanged(false); }
        else {
            QString tmpPath = logPath + ".tmp"; QFile tmpFile(tmpPath);
            if (tmpFile.open(QIODevice::WriteOnly)) {
                tmpFile.write(QJsonDocument(QJsonArray::fromStringList(current)).toJson()); tmpFile.close();
                MoveFileExW(tmpPath.toStdWString().c_str(), logPath.toStdWString().c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
    }
}

void MetadataManager::addToSyncLog(const std::wstring& dirPath) {
    QString path = QString::fromStdWString(dirPath);
    QString logPath = qApp->applicationDirPath() + "/Synchronize.scch";
    QStringList currentDirs;
    if (QFile::exists(logPath)) { QFile file(logPath); if (file.open(QIODevice::ReadOnly)) currentDirs = QJsonDocument::fromJson(file.readAll()).toVariant().toStringList(); }
    if (!currentDirs.contains(path)) {
        currentDirs << path; QJsonArray arr = QJsonArray::fromStringList(currentDirs);
        QString tmpPath = logPath + ".tmp"; QFile tmpFile(tmpPath);
        if (tmpFile.open(QIODevice::WriteOnly)) {
            tmpFile.write(QJsonDocument(arr).toJson()); tmpFile.close();
            if (MoveFileExW(tmpPath.toStdWString().c_str(), logPath.toStdWString().c_str(), MOVEFILE_REPLACE_EXISTING)) emit pendingSyncChanged(true);
        }
    }
}
void MetadataManager::saveSyncLog() {}

QStringList MetadataManager::searchInCache(const QString& keyword) {
    QStringList results; if (keyword.isEmpty()) return results;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        const std::wstring& path = it->first; const RuntimeMeta& meta = it->second;
        QString qPath = QString::fromStdWString(path); QString qNote = QString::fromStdWString(meta.note);
        bool match = qPath.contains(keyword, Qt::CaseInsensitive) || qNote.contains(keyword, Qt::CaseInsensitive);
        if (!match) { for (const QString& tag : meta.tags) { if (tag.contains(keyword, Qt::CaseInsensitive)) { match = true; break; } } }
        if (match) results << qPath;
    }
    return results;
}

} // namespace ArcMeta
