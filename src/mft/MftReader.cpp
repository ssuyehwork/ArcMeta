#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MftReader.h"
#include "UsnWatcher.h"
#include <winioctl.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <algorithm>
#include <execution>
#include <mutex>
#include <numeric>
#include <filesystem>
#include <QDebug>
#include <QRegularExpression>
#include <QDir>
#include <QDateTime>

namespace ArcMeta {

static int64_t filetimeToUnixMs(int64_t filetime) {
    // 2026-05-14 物理对标 Windows FILETIME 标准 (1601 Epoch to 1970 Unix)
    if (filetime <= 0) return 0;
    // 116444736000000000LL 是 1601 到 1970 的 100纳秒数
    // 10000LL 将 100纳秒 转换为 毫秒 (1ms = 10,000 * 100ns)
    return (filetime - 116444736000000000LL) / 10000LL;
}

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

MftReader::MftReader() {
    clearInternal();
}

MftReader::~MftReader() {
    clear();
}

void MftReader::clearInternal() {
    m_frns.clear();
    m_parent_frns.clear();
    m_sizes.clear();
    m_timestamps.clear();
    m_name_offsets.clear();
    m_attributes.clear();
    m_string_pool.clear();
    m_drive_list.clear();
    m_drive_active_mask = 0;
    m_frn_to_idx.clear();
    m_parent_to_children.clear();
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        m_path_cache.clear();
    }
    m_next_usns.clear();
    m_sorted_indices.clear();
    m_isInitialized = false;
    m_dirty_count = 0;
}

void MftReader::clear() {
    std::vector<UsnWatcher*> toStop;
    {
        QWriteLocker lock(&m_dataLock);
        toStop = std::move(m_watchers);
        m_watchers.clear();
    }
    for (auto* w : toStop) { if (w) { w->stop(); delete w; } }
    QWriteLocker lock(&m_dataLock);
    clearInternal();
}

void MftReader::updateActiveDrives(const QStringList& activeDrives) {
    // 2026-05-14 核心修正：使用原子掩码替代 QWriteLocker，消除 UI 线程同步死锁风险
    uint32_t mask = 0;
    QReadLocker lock(&m_dataLock);
    for (const QString& d : activeDrives) {
        std::wstring vol = d.toStdWString();
        if (vol.size() > 2 && vol.back() == L'\\') vol.pop_back();
        for (size_t i = 0; i < m_drive_list.size(); ++i) {
            if (m_drive_list[i] == vol) {
                mask |= (1 << i);
                break;
            }
        }
    }
    m_drive_active_mask.store(mask, std::memory_order_relaxed);
}

void MftReader::buildIndex(const QStringList& drives) {
    updateActiveDrives(drives);

    std::vector<std::wstring> toScan;
    {
        QReadLocker lock(&m_dataLock);
        for (const QString& d : drives) {
            std::wstring vol = d.toStdWString();
            if (vol.size() > 2 && vol.back() == L'\\') vol.pop_back();
            if (std::find(m_drive_list.begin(), m_drive_list.end(), vol) == m_drive_list.end()) {
                toScan.push_back(vol);
            }
        }
    }

    struct ScannedDrive {
        std::wstring volume;
        MftReader::DriveResult res; 
        bool success = false;
    };
    std::vector<ScannedDrive> scannedResults(toScan.size());
    std::vector<int> scanIndices((int)toScan.size());
    std::iota(scanIndices.begin(), scanIndices.end(), 0);
    std::for_each(std::execution::par, scanIndices.begin(), scanIndices.end(), [&](int i) {
        scannedResults[i].volume = toScan[i];
        scannedResults[i].success = loadMftDirect(toScan[i], scannedResults[i].res);
    });

    QWriteLocker lock(&m_dataLock);
    std::vector<UsnWatcher*> newWatchers;
    for (auto& sr : scannedResults) {
        if (!sr.success || sr.res.entries.empty()) continue;
        
        size_t dIdx = m_drive_list.size();
        m_drive_list.push_back(sr.volume);
        if (dIdx < 32) m_drive_active_mask.fetch_or(1 << dIdx);
        m_next_usns[sr.volume] = sr.res.nextUsn;
        mergeDriveResult(sr.volume, sr.res, dIdx);
        saveDriveToCacheInternal(dIdx);
        
        auto* w = new UsnWatcher(sr.volume, sr.res.nextUsn, nullptr);
        m_watchers.push_back(w);
        newWatchers.push_back(w);
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    lock.unlock();
    for (auto* w : newWatchers) w->start();
}

bool MftReader::loadFromCache() {
    std::filesystem::path cacheDir = "ArcMeta/cache";
    if (!std::filesystem::exists(cacheDir)) return false;

    QWriteLocker lock(&m_dataLock);
    clearInternal();

    for (auto const& entry : std::filesystem::directory_iterator{cacheDir}) {
        if (entry.path().extension() == ".scch") {
            MftReader::DriveResult dr;
            std::vector<uint64_t> f, pf;
            std::vector<int64_t> s, t;
            std::vector<uint32_t> no, attr;
            std::vector<uint8_t> sp;
            std::unordered_map<std::string, uint64_t> usnMap;

            if (ScchCache::load(entry.path().string().c_str(), f, pf, s, t, no, attr, sp, usnMap) == ScchResult::Ok) {
                size_t dIdx = m_drive_list.size();
                size_t oldPoolSize = m_string_pool.size();
                size_t count = f.size();

                m_frns.reserve(m_frns.size() + count);
                m_parent_frns.reserve(m_parent_frns.size() + count);
                m_sizes.reserve(m_sizes.size() + count);
                m_timestamps.reserve(m_timestamps.size() + count);
                m_name_offsets.reserve(m_name_offsets.size() + count);
                m_attributes.reserve(m_attributes.size() + count);

                for (size_t i = 0; i < count; ++i) {
                    m_frns.push_back(f[i]);
                    m_parent_frns.push_back((static_cast<uint64_t>(dIdx) << 48) | (pf[i] & 0x0000FFFFFFFFFFFFull));
                    m_sizes.push_back(s[i]);
                    m_timestamps.push_back(t[i]);
                    m_name_offsets.push_back(no[i] + (uint32_t)oldPoolSize);
                    m_attributes.push_back(attr[i]);
                }
                m_string_pool.insert(m_string_pool.end(), sp.begin(), sp.end());

                for (const auto& [drive, usn] : usnMap) {
                    std::wstring wDrive = QString::fromStdString(drive).toStdWString();
                    m_drive_list.push_back(wDrive);
                    m_next_usns[wDrive] = usn;
                }
            }
        }
    }

    if (m_frns.empty()) return false;
    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;
    return true;
}

bool MftReader::saveToCache() {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return false;
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
    std::vector<uint64_t> f, pf;
    std::vector<int64_t> s, t;
    std::vector<uint32_t> no, attr;
    std::vector<uint8_t> sp;
    std::unordered_map<uint32_t, uint32_t> offsetMap;

    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0 && (m_parent_frns[i] >> 48) == driveIdx) {
            f.push_back(m_frns[i]);
            pf.push_back(m_parent_frns[i] & 0x0000FFFFFFFFFFFFull);
            s.push_back(m_sizes[i]);
            t.push_back(m_timestamps[i]);
            attr.push_back(m_attributes[i]);
            uint32_t oldOff = m_name_offsets[i];
            if (offsetMap.find(oldOff) == offsetMap.end()) {
                uint32_t newOff = (uint32_t)sp.size();
                const char* ptr = reinterpret_cast<const char*>(m_string_pool.data() + oldOff);
                size_t len = strlen(ptr) + 1;
                sp.insert(sp.end(), ptr, ptr + len);
                offsetMap[oldOff] = newOff;
            }
            no.push_back(offsetMap[oldOff]);
        }
    }
    std::unordered_map<std::string, uint64_t> usnMap;
    usnMap[QString::fromStdWString(volume).toStdString()] = m_next_usns[volume];
    QString path = QString("ArcMeta/cache/%1.scch").arg(QString::fromStdWString(volume).left(1));
    return ScchCache::save(path.toStdString().c_str(), f, pf, s, t, no, attr, sp, usnMap);
}

QString MftReader::getName(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_name_offsets.size()) return QString();
    return QString::fromUtf8(reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[index]));
}

int64_t MftReader::getSize(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_sizes.size()) return 0;
    return m_sizes[index];
}

int64_t MftReader::getModifyTime(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_timestamps.size()) return 0;
    return m_timestamps[index];
}

uint32_t MftReader::getAttributes(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_attributes.size()) return 0;
    return m_attributes[index];
}

uint64_t MftReader::getFrn(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_frns.size()) return 0;
    return m_frns[index];
}

bool MftReader::isDirectory(int index) const {
    return (getAttributes(index) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int MftReader::totalCount() const {
    QReadLocker lock(&m_dataLock);
    return (int)m_frns.size();
}

QString MftReader::getFullPath(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_frns.size()) return QString();
    uint64_t frn = m_frns[index];
    size_t dIdx = static_cast<size_t>(m_parent_frns[index] >> 48);
    std::wstring vol = (dIdx < m_drive_list.size()) ? m_drive_list[dIdx] : L"C:";
    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFast(vol, frn));
}

std::wstring MftReader::getPathFast(const std::wstring& volume, uint64_t frn) {
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        auto it = m_path_cache.find(frn);
        if (it != m_path_cache.end()) return it->second;
    }
    std::vector<std::wstring> segments;
    uint64_t cur = frn;
    std::unordered_set<uint64_t> vis;
    while (true) {
        auto idxIt = m_frn_to_idx.find(cur);
        if (idxIt == m_frn_to_idx.end() || vis.count(cur)) break;
        vis.insert(cur);
        uint32_t idx = idxIt->second;
        const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
        segments.push_back(QString::fromUtf8(p).toStdWString());
        uint64_t parent = m_parent_frns[idx] & 0x0000FFFFFFFFFFFFull;
        if (parent == 5 || parent == cur || parent == 0) break;
        cur = parent;
    }
    if (segments.empty()) return L"";
    std::wstring path = volume;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) path += L"\\" + *it;
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        // 2026-05-14 内存优化：简单的缓存淘汰策略，防止路径缓存无限增长
        if (m_path_cache.size() > 100000) {
            auto it_clear = m_path_cache.begin();
            for (int i = 0; i < 1000; ++i) it_clear = m_path_cache.erase(it_clear);
        }
        m_path_cache[frn] = path;
    }
    return path;
}

QVector<int> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, 
                              const QStringList& extensionList, bool includeHidden, bool includeSystem) {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return {};

    bool hasQuery = !query.isEmpty();
    bool hasExt = !extensionList.isEmpty();

    QRegularExpression re;
    QByteArray queryUtf8;
    if (hasQuery) {
        if (useRegex) {
            re = QRegularExpression(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        } else {
            queryUtf8 = query.toUtf8();
        }
    }

    std::vector<QByteArray> extUtf8;
    if (hasExt) {
        for (const QString& ex : extensionList) {
            QString normalized = (ex.startsWith('.') ? ex : "." + ex);
            extUtf8.push_back(normalized.toUtf8());
        }
    }

    std::mutex mtx;
    std::vector<int> finalRes;
    finalRes.reserve(m_frns.size() / 16);

    size_t total = m_frns.size();
    const size_t grainSize = 4096;
    size_t numChunks = (total + grainSize - 1) / grainSize;
    std::vector<size_t> chunkIndices(numChunks);
    std::iota(chunkIndices.begin(), chunkIndices.end(), 0);

    std::for_each(std::execution::par, chunkIndices.begin(), chunkIndices.end(), [&](size_t chunkIdx) {
        std::vector<int> localRes;
        localRes.reserve(grainSize / 8);
        size_t startPos = chunkIdx * grainSize;
        size_t endPos = (std::min)(startPos + grainSize, total);

        for (size_t i = startPos; i < endPos; ++i) {
            if (m_frns[i] == 0) continue;
            
            size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
            if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;

            uint32_t at = m_attributes[i];
            if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;
            const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
            if (!hasQuery && !hasExt) { localRes.push_back((int)i); continue; }

            // 2026-05-14 性能优化：在字节流上直接操作，避免 QString 构造开销
            if (hasExt) {
                bool extMatch = false;
                size_t nameLen = strlen(p);
                for (const auto& ex : extUtf8) {
                    if (nameLen >= (size_t)ex.size()) {
                        if (_stricmp(p + nameLen - ex.size(), ex.constData()) == 0) {
                            extMatch = true;
                            break;
                        }
                    }
                }
                if (!extMatch) continue;
            }

            if (!hasQuery) {
                localRes.push_back((int)i);
            } else {
                bool match = false;
                if (useRegex) {
                    match = re.match(QString::fromUtf8(p)).hasMatch();
                } else {
                    if (caseSensitive) {
                        match = (strstr(p, queryUtf8.constData()) != nullptr);
                    } else {
                        // 使用 Windows API 高效执行大小写无关子串查找
                        match = (StrStrIA(p, queryUtf8.constData()) != nullptr);
                    }
                }
                if (match) localRes.push_back((int)i);
            }
        }
        if (!localRes.empty()) { std::lock_guard<std::mutex> l(mtx); finalRes.insert(finalRes.end(), localRes.begin(), localRes.end()); }
    });
    return QVector<int>(finalRes.begin(), finalRes.end());
}

void MftReader::updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume) {
    uint64_t frn = record->FileReferenceNumber;
    uint64_t fileSize = 0;

    // 2026-05-14 性能优化：将耗时的磁盘 I/O 移出锁范围
    if (!(record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring dev = L"\\\\.\\" + volume;
        HANDLE hVol = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hVol != INVALID_HANDLE_VALUE) {
            FILE_ID_DESCRIPTOR id = {0};
            id.dwSize = sizeof(FILE_ID_DESCRIPTOR);
            id.Type = FileIdType;
            id.FileId.QuadPart = frn;
            HANDLE hFile = OpenFileById(hVol, &id, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 0);
            if (hFile != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER fs;
                if (GetFileSizeEx(hFile, &fs)) fileSize = (uint64_t)fs.QuadPart;
                CloseHandle(hFile);
            }
            CloseHandle(hVol);
        }
    }

    QWriteLocker lock(&m_dataLock);
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(reinterpret_cast<uint8_t*>(record) + record->FileNameOffset), record->FileNameLength / 2);
    size_t dIdx = 0;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; break; } }
    uint64_t encodedPf = (static_cast<uint64_t>(dIdx) << 48) | (record->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull);
    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_parent_frns[idx] = encodedPf;
        m_attributes[idx] = record->FileAttributes;
        m_timestamps[idx] = filetimeToUnixMs(record->TimeStamp.QuadPart);
        m_sizes[idx] = fileSize;
        QByteArray utf8 = name.toUtf8();
        uint32_t oldOff = m_name_offsets[idx];
        const char* oldPtr = reinterpret_cast<const char*>(m_string_pool.data() + oldOff);
        size_t oldLen = strlen(oldPtr);
        if ((size_t)utf8.size() <= oldLen) {
            memcpy(m_string_pool.data() + oldOff, utf8.constData(), utf8.size());
            m_string_pool[oldOff + utf8.size()] = '\0';
            if ((size_t)utf8.size() < oldLen) m_wasted_string_bytes += (oldLen - utf8.size());
        } else {
            m_wasted_string_bytes += (oldLen + 1);
            m_name_offsets[idx] = (uint32_t)m_string_pool.size();
            m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
            m_string_pool.push_back('\0');
        }
    } else {
        uint32_t newIdx = (uint32_t)m_frns.size();
        m_frns.push_back(frn);
        m_parent_frns.push_back(encodedPf);
        m_sizes.push_back(fileSize);
        m_timestamps.push_back(filetimeToUnixMs(record->TimeStamp.QuadPart));
        m_attributes.push_back(record->FileAttributes);
        QByteArray utf8 = name.toUtf8();
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
        m_string_pool.push_back('\0');
        m_frn_to_idx[frn] = newIdx;
    }
    { std::lock_guard<std::mutex> l(m_pathCacheMutex); m_path_cache.erase(frn); }
    m_next_usns[volume] = record->Usn;
    m_dirty_count++;
    if (m_dirty_count >= 1000) { m_dirty_count = 0; saveDriveToCacheInternal(dIdx); }
    emit dataChanged();
}

void MftReader::removeEntryByFrn(const std::wstring& volume, uint64_t frn) {
    Q_UNUSED(volume);
    QWriteLocker lock(&m_dataLock);
    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_frns[idx] = 0;
        m_frn_to_idx.erase(it);
        m_dead_count++;
        const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
        m_wasted_string_bytes += (strlen(p) + 1);

        { std::lock_guard<std::mutex> l(m_pathCacheMutex); m_path_cache.erase(frn); }

        if (m_dead_count > 50000 || m_wasted_string_bytes > 10 * 1024 * 1024) {
            compact();
        }

        emit dataChanged();
    }
}

void MftReader::compact() {
    // 2026-05-14 内存管理优化：执行碎片整理，回收无效条目和字符串池空间
    std::vector<uint64_t>  new_frns;
    std::vector<uint64_t>  new_parent_frns;
    std::vector<int64_t>   new_sizes;
    std::vector<int64_t>   new_timestamps;
    std::vector<uint32_t>  new_name_offsets;
    std::vector<uint32_t>  new_attributes;
    std::vector<uint8_t>   new_string_pool;

    size_t count = m_frns.size();
    new_frns.reserve(count - m_dead_count);
    new_parent_frns.reserve(count - m_dead_count);
    new_sizes.reserve(count - m_dead_count);
    new_timestamps.reserve(count - m_dead_count);
    new_name_offsets.reserve(count - m_dead_count);
    new_attributes.reserve(count - m_dead_count);
    new_string_pool.reserve(m_string_pool.size() - m_wasted_string_bytes);

    m_frn_to_idx.clear();
    for (size_t i = 0; i < count; ++i) {
        if (m_frns[i] == 0) continue;

        uint32_t newIdx = (uint32_t)new_frns.size();
        m_frn_to_idx[m_frns[i]] = newIdx;

        new_frns.push_back(m_frns[i]);
        new_parent_frns.push_back(m_parent_frns[i]);
        new_sizes.push_back(m_sizes[i]);
        new_timestamps.push_back(m_timestamps[i]);
        new_attributes.push_back(m_attributes[i]);

        const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
        size_t len = strlen(name) + 1;
        new_name_offsets.push_back((uint32_t)new_string_pool.size());
        new_string_pool.insert(new_string_pool.end(), name, name + len);
    }

    m_frns = std::move(new_frns);
    m_parent_frns = std::move(new_parent_frns);
    m_sizes = std::move(new_sizes);
    m_timestamps = std::move(new_timestamps);
    m_name_offsets = std::move(new_name_offsets);
    m_attributes = std::move(new_attributes);
    m_string_pool = std::move(new_string_pool);

    m_dead_count = 0;
    m_wasted_string_bytes = 0;
    rebuildFrnToIndexMap();
    buildSortedIndices();
}

bool MftReader::loadMftDirect(const std::wstring& volume, MftReader::DriveResult& result) {
    std::wstring dev = L"\\\\.\\" + volume;
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    USN_JOURNAL_DATA_V0 j; DWORD cb;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &j, sizeof(j), &cb, NULL)) { CloseHandle(h); return false; }
    result.nextUsn = j.NextUsn;
    MFT_ENUM_DATA_V0 ed = {0}; ed.HighUsn = j.NextUsn;
    std::vector<uint8_t> buf(1024 * 1024);
    while (DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &ed, sizeof(ed), buf.data(), (DWORD)buf.size(), &cb, NULL)) {
        if (cb < 8) break;
        uint8_t* p = buf.data() + 8; uint8_t* end = buf.data() + cb;
        while (p < end) {
            ::USN_RECORD_V2* rec = reinterpret_cast<::USN_RECORD_V2*>(p);
            MftReader::RawEntry e; e.frn = rec->FileReferenceNumber; e.parentFrn = rec->ParentFileReferenceNumber;
            // 2026-05-14 深度修正：从磁盘原始记录中获取真实的物理大小。
            // 文件夹属性下，物理大小无意义，统一设为 0；文件则初始化。
            e.size = 0;
            if (!(rec->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                // 2026-05-14 核心修正：修复 OpenFileById 传参错误，正确获取物理大小
                FILE_ID_DESCRIPTOR id = {0};
                id.dwSize = sizeof(FILE_ID_DESCRIPTOR);
                id.Type = FileIdType;
                id.FileId.QuadPart = rec->FileReferenceNumber;

                HANDLE hFile = OpenFileById(h, &id, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 0);
                if (hFile != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER fs;
                    if (GetFileSizeEx(hFile, &fs)) e.size = (uint64_t)fs.QuadPart;
                    CloseHandle(hFile);
                }
            }
            e.attributes = rec->FileAttributes; e.modifyTime = filetimeToUnixMs(rec->TimeStamp.QuadPart);
            QString n = QString::fromUtf16(reinterpret_cast<const char16_t*>(p + rec->FileNameOffset), rec->FileNameLength / 2);
            e.nameUtf8 = n.toUtf8().toStdString();
            result.entries.push_back(std::move(e));
            p += rec->RecordLength;
        }
        ed.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buf.data());
    }
    CloseHandle(h);
    return !result.entries.empty();
}

void MftReader::mergeDriveResult(const std::wstring& volume, const MftReader::DriveResult& result, size_t driveIdx) {
    Q_UNUSED(volume);
    size_t count = result.entries.size();
    m_frns.reserve(m_frns.size() + count);
    m_parent_frns.reserve(m_parent_frns.size() + count);
    m_sizes.reserve(m_sizes.size() + count);
    m_timestamps.reserve(m_timestamps.size() + count);
    m_name_offsets.reserve(m_name_offsets.size() + count);
    m_attributes.reserve(m_attributes.size() + count);
    for (const auto& e : result.entries) {
        m_frns.push_back(e.frn);
        m_parent_frns.push_back((static_cast<uint64_t>(driveIdx) << 48) | (e.parentFrn & 0x0000FFFFFFFFFFFFull));
        m_sizes.push_back(e.size); // 2026-05-14 修正：将扫描到的大小压入 SoA
        m_timestamps.push_back(e.modifyTime); m_attributes.push_back(e.attributes);
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), e.nameUtf8.begin(), e.nameUtf8.end());
        m_string_pool.push_back('\0');
    }
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear(); m_parent_to_children.clear();
    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            m_frn_to_idx[m_frns[i]] = (uint32_t)i;
            uint64_t p = m_parent_frns[i] & 0x0000FFFFFFFFFFFFull;
            if (p != 0) m_parent_to_children[p].push_back(m_frns[i]);
        }
    }
}

void MftReader::buildSortedIndices() {
    m_sorted_indices.resize(m_frns.size());
    std::iota(m_sorted_indices.begin(), m_sorted_indices.end(), 0);
    std::sort(std::execution::par, m_sorted_indices.begin(), m_sorted_indices.end(), [this](uint32_t a, uint32_t b) {
        const char* s1 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[a]);
        const char* s2 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[b]);
        return _stricmp(s1, s2) < 0;
    });
}

} // namespace ArcMeta
