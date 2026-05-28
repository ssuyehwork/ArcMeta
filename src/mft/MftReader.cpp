#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MftReader.h"
#include "MftDataStore.h"
#include "NtfsEngine.h"
#include "SyncManager.h"
#include "UsnWatcher.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <algorithm>
#include <execution>
#include <mutex>
#include <numeric>
#include <filesystem>
#include <QDebug>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <QFileIconProvider>
#include <QFileInfo>

namespace ArcMeta {

MftReader& MftReader::instance() {
    static MftReader inst;
    static std::once_flag flag;
    std::call_once(flag, []() {
        NtfsEngine::enablePrivilege(SE_BACKUP_NAME);
        NtfsEngine::enablePrivilege(SE_RESTORE_NAME);
    });
    return inst;
}

MftReader::MftReader() {
    m_data = std::make_shared<MftDataStore>();
    m_syncMgr = new SyncManager(this);
    connect(m_syncMgr, &SyncManager::usnRecordReceived, this, &MftReader::onUsnRecordReceived);
    connect(m_syncMgr, &SyncManager::journalInvalidated, this, &MftReader::onJournalInvalidated);
}

MftReader::~MftReader() {
    clear();
}

void MftReader::clear() {
    if (m_syncMgr) m_syncMgr->stopAll();
    QWriteLocker lock(&m_dataLock);
    m_data = std::make_shared<MftDataStore>();
    m_drive_list.clear();
    m_drive_active_mask = 0;
    m_next_usns.clear();
    m_isInitialized = false;
    m_dirty_count = 0;
    { std::lock_guard<std::mutex> pcLock(m_pathCacheMutex); m_path_cache.clear(); }
    { QWriteLocker icLock(&m_iconCacheLock); m_icon_cache.clear(); }
}

void MftReader::updateActiveDrives(const QStringList& activeDrives) {
    uint32_t mask = 0;
    QReadLocker lock(&m_dataLock);
    for (const QString& d : activeDrives) {
        std::wstring vol = d.toStdWString();
        if (vol.size() > 1 && (vol.back() == L'\\' || vol.back() == L'/')) vol.pop_back();
        for (size_t i = 0; i < m_drive_list.size(); ++i) {
            if (_wcsicmp(m_drive_list[i].c_str(), vol.c_str()) == 0) { mask |= (1 << i); break; }
        }
    }
    m_drive_active_mask.store(mask, std::memory_order_relaxed);
}

bool MftReader::isDriveIndexed(const QString& drive) {
    std::wstring vol = drive.toStdWString();
    if (vol.size() > 1 && (vol.back() == L'\\' || vol.back() == L'/')) vol.pop_back();
    QReadLocker lock(&m_dataLock);
    for (const auto& indexedVol : m_drive_list) {
        if (_wcsicmp(indexedVol.c_str(), vol.c_str()) == 0) return true;
    }
    return false;
}

void MftReader::buildIndex(const QStringList& drives) {
    updateActiveDrives(drives);
    std::vector<std::wstring> toScan;
    {
        QReadLocker lock(&m_dataLock);
        for (const QString& d : drives) {
            std::wstring vol = d.toStdWString();
            if (vol.size() > 1 && (vol.back() == L'\\' || vol.back() == L'/')) vol.pop_back();
            bool found = false;
            for (const auto& indexedVol : m_drive_list) {
                if (_wcsicmp(indexedVol.c_str(), vol.c_str()) == 0) { found = true; break; }
            }
            if (!found) toScan.push_back(vol);
        }
    }
    if (toScan.empty()) { QReadLocker lock(&m_dataLock); if (m_isInitialized) return; }

    struct ScanTask { std::wstring volume; DriveResult res; bool success; };
    std::vector<ScanTask> tasks(toScan.size());
    std::vector<int> taskIndices((int)toScan.size());
    std::iota(taskIndices.begin(), taskIndices.end(), 0);
    std::for_each((std::execution::par), taskIndices.begin(), taskIndices.end(), [&](int i) {
        tasks[i].volume = toScan[i];
        tasks[i].success = NtfsEngine::loadMftDirect(toScan[i], tasks[i].res);
    });

    QWriteLocker lock(&m_dataLock);
    for (auto& task : tasks) {
        if (!task.success || task.res.entries.empty()) continue;
        uint32_t dIdx = (uint32_t)m_drive_list.size();
        m_drive_list.push_back(task.volume);
        if (dIdx < 32) m_drive_active_mask.fetch_or(1 << dIdx);
        m_next_usns[task.volume] = task.res.nextUsn;
        m_data->m_frns.reserve(m_data->m_frns.size() + task.res.entries.size());
        for (const auto& e : task.res.entries) {
            uint32_t idx = (uint32_t)m_data->m_frns.size();
            m_data->m_frns.push_back(e.frn);
            m_data->m_drive_indices.push_back(dIdx);
            m_data->m_parent_frns.push_back(e.parentFrn);
            m_data->m_sizes.push_back(e.size);
            m_data->m_timestamps.push_back(e.modifyTime);
            m_data->m_attributes.push_back(e.attributes);
            m_data->m_metadata_fetched.push_back(0);
            m_data->m_name_offsets.push_back(m_data->addString(e.nameUtf8));
            m_data->m_key_to_idx[{dIdx, e.frn}] = idx;
        }
        saveDriveToCacheInternal(dIdx);
        m_syncMgr->startWatching(task.volume, task.res.nextUsn);
    }
    m_data->m_sorted_indices.resize(m_data->m_frns.size());
    std::iota(m_data->m_sorted_indices.begin(), m_data->m_sorted_indices.end(), 0);
    std::sort((std::execution::par), m_data->m_sorted_indices.begin(), m_data->m_sorted_indices.end(), [this](uint32_t a, uint32_t b) {
        const char* s1 = m_data->getNamePtr(a);
        const char* s2 = m_data->getNamePtr(b);
        return _stricmp(s1 ? s1 : "", s2 ? s2 : "") < 0;
    });
    m_isInitialized = true;
}

bool MftReader::loadFromCache() {
    std::filesystem::path cacheDir = "ArcMeta/cache";
    if (!std::filesystem::exists(cacheDir)) return false;
    clear(); 
    for (auto const& entry : std::filesystem::directory_iterator{cacheDir}) {
        if (entry.path().extension() == ".scch") {
            std::vector<Frn128> f, pf; std::vector<uint32_t> di, si;
            std::vector<int64_t> s, t; std::vector<uint32_t> no, attr;
            std::vector<uint8_t> sp, mf; std::unordered_map<std::string, uint64_t> usnMap;
            if (ScchCache::load(entry.path().string().c_str(), f, di, pf, s, t, no, attr, mf, sp, si, usnMap) == ScchResult::Ok) {
                QWriteLocker lock(&m_dataLock);
                uint32_t oldDCount = (uint32_t)m_drive_list.size();
                size_t oldPoolSize = m_data->m_string_pool.size();
                size_t baseIdx = m_data->m_frns.size();
                m_data->m_frns.insert(m_data->m_frns.end(), f.begin(), f.end());
                m_data->m_parent_frns.insert(m_data->m_parent_frns.end(), pf.begin(), pf.end());
                m_data->m_sizes.insert(m_data->m_sizes.end(), s.begin(), s.end());
                m_data->m_timestamps.insert(m_data->m_timestamps.end(), t.begin(), t.end());
                m_data->m_attributes.insert(m_data->m_attributes.end(), attr.begin(), attr.end());
                m_data->m_metadata_fetched.insert(m_data->m_metadata_fetched.end(), mf.begin(), mf.end());
                m_data->m_string_pool.insert(m_data->m_string_pool.end(), sp.begin(), sp.end());
                for (size_t i = 0; i < f.size(); ++i) {
                    uint32_t dIdx = di[i] + oldDCount;
                    m_data->m_drive_indices.push_back(dIdx);
                    m_data->m_name_offsets.push_back(no[i] + (uint32_t)oldPoolSize);
                    m_data->m_key_to_idx[{dIdx, f[i]}] = (uint32_t)(baseIdx + i);
                }
                std::wstring dName;
                for (const auto& [drive, usn] : usnMap) {
                    dName = QString::fromStdString(drive).toStdWString();
                    m_drive_list.push_back(dName);
                    m_next_usns[dName] = usn;
                }
                lock.unlock();
                emit driveLoaded(QString::fromStdWString(dName), (int)f.size(), (int)m_data->m_frns.size());
                m_syncMgr->startWatching(dName, usnMap.begin()->second);
            }
        }
    }
    QWriteLocker lock(&m_dataLock);
    if (m_data->m_frns.empty()) return false;
    m_data->m_sorted_indices.resize(m_data->m_frns.size());
    std::iota(m_data->m_sorted_indices.begin(), m_data->m_sorted_indices.end(), 0);
    std::sort((std::execution::par), m_data->m_sorted_indices.begin(), m_data->m_sorted_indices.end(), [this](uint32_t a, uint32_t b) {
        const char* s1 = m_data->getNamePtr(a);
        const char* s2 = m_data->getNamePtr(b);
        return _stricmp(s1 ? s1 : "", s2 ? s2 : "") < 0;
    });
    m_isInitialized = true;
    return true;
}

bool MftReader::saveToCache() {
    QReadLocker lock(&m_dataLock);
    for (size_t i = 0; i < m_drive_list.size(); ++i) saveDriveToCacheInternal(i);
    return true;
}

bool MftReader::saveDriveToCache(size_t driveIdx) {
    QReadLocker lock(&m_dataLock);
    return saveDriveToCacheInternal(driveIdx);
}

bool MftReader::saveDriveToCacheInternal(size_t driveIdx) {
    if (driveIdx >= m_drive_list.size()) return false;
    std::wstring volume = m_drive_list[driveIdx];
    std::vector<Frn128> f, pf; std::vector<uint32_t> di, no, attr, ds;
    std::vector<int64_t> s, t; std::vector<uint8_t> sp, mf;
    std::unordered_map<uint32_t, uint32_t> offsetMap;
    std::unordered_map<uint32_t, uint32_t> gToL;
    for (size_t i = 0; i < m_data->m_frns.size(); ++i) {
        if (!m_data->m_frns[i].isZero() && m_data->m_drive_indices[i] == (uint32_t)driveIdx) {
            uint32_t lIdx = (uint32_t)f.size(); gToL[(uint32_t)i] = lIdx;
            f.push_back(m_data->m_frns[i]); di.push_back(0);
            pf.push_back(m_data->m_parent_frns[i]);
            s.push_back(m_data->m_sizes[i]); t.push_back(m_data->m_timestamps[i]);
            attr.push_back(m_data->m_attributes[i]); mf.push_back(m_data->m_metadata_fetched[i]);
            uint32_t oOff = m_data->m_name_offsets[i];
            if (offsetMap.find(oOff) == offsetMap.end()) {
                uint32_t nOff = (uint32_t)sp.size();
                const char* ptr = m_data->getNamePtr((uint32_t)i);
                size_t len = ptr ? strlen(ptr) + 1 : 0;
                if (len > 0) sp.insert(sp.end(), ptr, ptr + len);
                offsetMap[oOff] = nOff;
            }
            no.push_back(offsetMap[oOff]);
        }
    }
    for (uint32_t gIdx : m_data->m_sorted_indices) { if (gToL.count(gIdx)) ds.push_back(gToL[gIdx]); }
    std::unordered_map<std::string, uint64_t> usnMap;
    usnMap[QString::fromStdWString(volume).toStdString()] = m_next_usns[volume];
    QString path = QString("ArcMeta/cache/%1.scch").arg(QString::fromStdWString(volume).left(1));
    return ScchCache::save(path.toStdString().c_str(), f, di, pf, s, t, no, attr, mf, sp, ds, usnMap);
}

void MftReader::onUsnRecordReceived(const std::wstring& volume, const QByteArray& recordData) {
    updateEntryFromUsnRecord(recordData, volume);
}

void MftReader::onJournalInvalidated(const std::wstring& volume) {
    qDebug() << "[MftReader] Journal invalidated for" << QString::fromStdWString(volume);
}

void MftReader::updateEntryFromUsnRecord(const QByteArray& recordData, const std::wstring& volume) {
    const USN_RECORD_COMMON_HEADER* header = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(recordData.constData());
    Frn128 frn, parentFrn; uint64_t usn; uint32_t attr, reason; LARGE_INTEGER timestamp; WORD nLen, nOff;
    if (header->MajorVersion == 2) {
        const USN_RECORD_V2* r = reinterpret_cast<const USN_RECORD_V2*>(header);
        frn = Frn128(r->FileReferenceNumber); parentFrn = Frn128(r->ParentFileReferenceNumber);
        usn = r->Usn; attr = r->FileAttributes; reason = r->Reason; timestamp = r->TimeStamp;
        nLen = r->FileNameLength; nOff = r->FileNameOffset;
    } else if (header->MajorVersion == 3) {
        const USN_RECORD_V3* r = reinterpret_cast<const USN_RECORD_V3*>(header);
        memcpy(&frn, &r->FileReferenceNumber, 16); memcpy(&parentFrn, &r->ParentFileReferenceNumber, 16);
        usn = r->Usn; attr = r->FileAttributes; reason = r->Reason; timestamp = r->TimeStamp;
        nLen = r->FileNameLength; nOff = r->FileNameOffset;
    } else return;

    if (reason & USN_REASON_FILE_DELETE) { removeEntryByFrn(volume, frn); return; }
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(recordData.constData() + nOff), nLen / 2);
    QWriteLocker lock(&m_dataLock);
    int dIdx = -1;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (_wcsicmp(m_drive_list[i].c_str(), volume.c_str()) == 0) { dIdx = (int)i; break; } }
    if (dIdx == -1) return;
    FullKey key = {(uint32_t)dIdx, frn}; uint32_t fIdx = 0; bool isNew = false;
    auto it = m_data->m_key_to_idx.find(key);
    if (it != m_data->m_key_to_idx.end()) {
        fIdx = it->second; m_data->m_parent_frns[fIdx] = parentFrn; m_data->m_attributes[fIdx] = attr;
        m_data->m_timestamps[fIdx] = NtfsEngine::filetimeToUnixMs(timestamp.QuadPart);
        m_data->updateString(fIdx, name.toUtf8().toStdString());
    } else {
        fIdx = (uint32_t)m_data->m_frns.size(); isNew = true;
        m_data->m_frns.push_back(frn); m_data->m_drive_indices.push_back((uint32_t)dIdx);
        m_data->m_parent_frns.push_back(parentFrn); m_data->m_sizes.push_back(0);
        m_data->m_timestamps.push_back(NtfsEngine::filetimeToUnixMs(timestamp.QuadPart));
        m_data->m_attributes.push_back(attr); m_data->m_metadata_fetched.push_back(0);
        m_data->m_name_offsets.push_back(m_data->addString(name.toUtf8().toStdString()));
        m_data->m_key_to_idx[key] = fIdx;
    }
    { std::lock_guard<std::mutex> pcLock(m_pathCacheMutex); m_path_cache.erase(frn.low); }
    m_next_usns[volume] = usn;
    if (m_data->m_dead_count > 100000) performBackgroundCompact();
    lock.unlock();
    if (isNew) emit entryAdded(fIdx); else emit entryUpdated(fIdx);
}

void MftReader::removeEntryByFrn(const std::wstring& volume, Frn128 frn) {
    QWriteLocker lock(&m_dataLock);
    int dIdx = -1;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (_wcsicmp(m_drive_list[i].c_str(), volume.c_str()) == 0) { dIdx = (int)i; break; } }
    if (dIdx == -1) return;
    FullKey key = {(uint32_t)dIdx, frn};
    auto it = m_data->m_key_to_idx.find(key);
    if (it != m_data->m_key_to_idx.end()) {
        uint32_t idx = it->second; m_data->m_frns[idx] = {0, 0};
        m_data->m_key_to_idx.erase(it); m_data->m_dead_count++;
        auto itS = std::find(m_data->m_sorted_indices.begin(), m_data->m_sorted_indices.end(), idx);
        if (itS != m_data->m_sorted_indices.end()) m_data->m_sorted_indices.erase(itS);
        lock.unlock(); emit entryRemoved(frn.low); emit dataChanged(-1);
    }
}

void MftReader::performBackgroundCompact() {
    auto oldData = m_data;
    (void)QtConcurrent::run([this, oldData]() {
        auto newData = oldData->compact();
        QWriteLocker lock(&m_dataLock); m_data = newData; lock.unlock();
        emit dataChanged(-1);
    });
}

QString MftReader::getName(int i) const { QReadLocker l(&m_dataLock); return QString::fromUtf8(m_data->getNamePtr(i)); }
int64_t MftReader::getSize(int i) const { QReadLocker l(&m_dataLock); return (i>=0 && i<(int)m_data->m_sizes.size())?m_data->m_sizes[i]:0; }
int64_t MftReader::getModifyTime(int i) const { QReadLocker l(&m_dataLock); return (i>=0 && i<(int)m_data->m_timestamps.size())?m_data->m_timestamps[i]:0; }
uint32_t MftReader::getAttributes(int i) const { QReadLocker l(&m_dataLock); return (i>=0 && i<(int)m_data->m_attributes.size())?m_data->m_attributes[i]:0; }
Frn128 MftReader::getFrn(int i) const { QReadLocker l(&m_dataLock); return (i>=0 && i<(int)m_data->m_frns.size())?m_data->m_frns[i]:Frn128(); }
bool MftReader::isDirectory(int i) const { return (getAttributes(i) & FILE_ATTRIBUTE_DIRECTORY) != 0; }
bool MftReader::isMetadataFetched(int i) const { QReadLocker l(&m_dataLock); return (i>=0 && i<(int)m_data->m_metadata_fetched.size())?(m_data->m_metadata_fetched[i]==2):true; }
int MftReader::totalCount() const { QReadLocker l(&m_dataLock); return (int)m_data->m_frns.size(); }

int MftReader::getIndexByKey(uint32_t dIdx, Frn128 frn) const {
    QReadLocker l(&m_dataLock);
    auto it = m_data->m_key_to_idx.find({dIdx, frn});
    return (it != m_data->m_key_to_idx.end())?(int)it->second:-1;
}

int MftReader::getIndexByKey(uint64_t compositeKey) const {
    return getIndexByKey((uint32_t)(compositeKey >> 48), Frn128(compositeKey & 0x0000FFFFFFFFFFFFull));
}

uint64_t MftReader::getKeyByIndex(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_data->m_frns.size()) return 0;
    // 2026-06-xx 同步：返回兼容旧版的 64 位复合主键 (driveIdx << 48 | 48位FRN)
    uint64_t dIdx = static_cast<uint64_t>(m_data->m_drive_indices[index]);
    uint64_t frnLow = m_data->m_frns[index].low & 0x0000FFFFFFFFFFFFull;
    return (dIdx << 48) | frnLow;
}

QString MftReader::getFullPath(int i) const {
    QReadLocker l(&m_dataLock);
    if (i<0 || i>=(int)m_data->m_frns.size()) return QString();
    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFast(m_data->m_drive_indices[i], m_data->m_frns[i]));
}

std::wstring MftReader::getPathFast(uint32_t driveIdx, Frn128 frn) {
    uint64_t k = frn.low; { std::lock_guard<std::mutex> l(m_pathCacheMutex); auto it = m_path_cache.find(k); if (it != m_path_cache.end()) return it->second; }
    std::vector<std::wstring> segs; Frn128 cur = frn;
    while (true) {
        auto idxIt = m_data->m_key_to_idx.find({driveIdx, cur}); if (idxIt == m_data->m_key_to_idx.end()) break;
        uint32_t idx = idxIt->second;
        const char* namePtr = m_data->getNamePtr(idx);
        if (namePtr) segs.push_back(QString::fromUtf8(namePtr).toStdWString());
        Frn128 p = m_data->m_parent_frns[idx]; if (p.low == 5 || p == cur || p.isZero()) break; cur = p;
    }
    if (segs.empty()) return L"";
    std::wstring path = (driveIdx < m_drive_list.size()) ? m_drive_list[driveIdx] : L"C:";
    for (auto it = segs.rbegin(); it != segs.rend(); ++it) path += L"\\" + *it;
    { std::lock_guard<std::mutex> l(m_pathCacheMutex); if (m_path_cache.size() > 200000) m_path_cache.clear(); m_path_cache[k] = path; }
    return path;
}

std::vector<uint64_t> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, const QStringList& extensionList, bool includeHidden, bool includeSystem, bool includeDollar) {
    QReadLocker lock(&m_dataLock); if (!m_isInitialized) return {};
    std::vector<uint64_t> res; size_t total = m_data->m_frns.size();
    for (size_t i = 0; i < total; ++i) {
        if (matchEntry((int)i, query, useRegex, caseSensitive, extensionList, includeHidden, includeSystem, includeDollar)) {
            res.push_back(m_data->m_frns[i].low);
        }
    }
    return res;
}

bool MftReader::matchEntry(int i, const QString& query, bool useRegex, bool caseSensitive, const QStringList& extensionList, bool includeHidden, bool includeSystem, bool includeDollar) const {
    if (i < 0 || i >= (int)m_data->m_frns.size() || m_data->m_frns[i].isZero()) return false;
    uint32_t dIdx = m_data->m_drive_indices[i];
    if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) return false;
    uint32_t at = m_data->m_attributes[i];
    if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) return false;
    if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) return false;
    const char* p = m_data->getNamePtr(i);
    if (!includeDollar && p && p[0] == '$') return false;
    if (query.isEmpty() && extensionList.isEmpty()) return true;
    if (!extensionList.isEmpty()) {
        bool extMatch = false; size_t nLen = p ? strlen(p) : 0;
        for (const QString& ex : extensionList) {
            QByteArray exU = (ex.startsWith('.') ? ex : "." + ex).toUtf8();
            if (nLen >= (size_t)exU.size() && _stricmp(p + nLen - exU.size(), exU.constData()) == 0) { extMatch = true; break; }
        }
        if (!extMatch) return false;
    }
    if (query.isEmpty()) return true;
    if (useRegex) return QRegularExpression(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption).match(QString::fromUtf8(p)).hasMatch();
    QByteArray qU = query.toUtf8(); return caseSensitive ? (p && strstr(p, qU.constData()) != nullptr) : (p && StrStrIA(p, qU.constData()) != nullptr);
}

void MftReader::requestMetadata(int i) {
    QWriteLocker wL(&m_dataLock);
    if (i<0 || i>=(int)m_data->m_frns.size() || m_data->m_frns[i].isZero() || m_data->m_metadata_fetched[i]!=0) return;
    m_data->m_metadata_fetched[i] = 1;
    Frn128 frn = m_data->m_frns[i]; uint32_t dIdx = m_data->m_drive_indices[i];
    std::wstring vol = m_drive_list[dIdx]; QString fPath = getFullPath(i); wL.unlock();
    (void)QtConcurrent::run([this, i, frn, vol, fPath]() {
        auto meta = NtfsEngine::getFileMetadata(vol, frn, fPath.toStdWString());
        if (meta.success) {
            QWriteLocker l(&m_dataLock);
            if (i<(int)m_data->m_frns.size() && m_data->m_frns[i] == frn) {
                m_data->m_sizes[i] = meta.size; m_data->m_timestamps[i] = meta.modifyTime;
                m_data->m_attributes[i] = meta.attributes; m_data->m_metadata_fetched[i] = 2;
                l.unlock(); emit dataChanged(i);
            }
        }
    });
}

QIcon MftReader::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    { QReadLocker l(&m_iconCacheLock); auto it = m_icon_cache.find(key); if (it != m_icon_cache.end()) return *it; }
    QFileIconProvider pr; QIcon ic = isDir ? pr.icon(QFileIconProvider::Folder) : pr.icon(QFileInfo("dummy." + key));
    if (ic.isNull()) ic = pr.icon(QFileIconProvider::File);
    { QWriteLocker l(&m_iconCacheLock); m_icon_cache[key] = ic; }
    return ic;
}

} // namespace ArcMeta
