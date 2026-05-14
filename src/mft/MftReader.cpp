#include "MftReader.h"
#include "UsnWatcher.h"
#include <winioctl.h>
#include <shlwapi.h>
#include <execution>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <unordered_set>
#include <QDateTime>
#include <QDebug>

#pragma comment(lib, "Shlwapi.lib")

namespace ArcMeta {

// 时间戳转换：Windows FILETIME (100ns since 1601) -> Unix 毫秒
inline int64_t filetimeToUnixMs(int64_t filetime) {
    return (filetime - 116444736000000000LL) / 10000LL;
}

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

MftReader::MftReader() {
    QWriteLocker lock(&m_dataLock);
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
    m_frn_to_idx.clear();
    m_parent_to_children.clear();
    m_path_cache.clear();
    m_next_usns.clear();
    m_drive_list.clear();
    m_sorted_indices.clear();
    m_isInitialized = false;
}

void MftReader::clear() {
    std::vector<UsnWatcher*> toStop;
    {
        QWriteLocker lock(&m_dataLock);
        toStop = std::move(m_watchers);
    }
    for (auto* w : toStop) {
        if (w) {
            w->stop();
            delete w;
        }
    }

    QWriteLocker lock(&m_dataLock);
    clearInternal();
}

void MftReader::buildIndex(const QStringList& drives) {
    clear();

    {
        QWriteLocker lock(&m_dataLock);

        QStringList targetDrives = drives.isEmpty() ? getAvailableDrives() : drives;

        for (const QString& drive : targetDrives) {
            if (isFixedDrive(drive)) {
                std::wstring volumePath = drive.toStdWString();
                if (volumePath.back() == L'\\') volumePath.pop_back();

                DriveResult result;
                if (loadMftDirect(volumePath, result)) {
                    mergeDriveResult(result);
                }
            }
        }

        rebuildFrnToIndexMap();
        buildSortedIndices();
        m_isInitialized = true;
    }

    saveToCache();
    
    {
        QWriteLocker lock(&m_dataLock);
        for (auto& [drive, usn] : m_next_usns) {
            auto* watcher = new UsnWatcher(drive, usn);
            m_watchers.push_back(watcher);
            watcher->start();
        }
    }
}

bool MftReader::loadFromCache() {
    clear();

    std::unordered_map<std::string, uint64_t> usnMap;
    {
        QWriteLocker lock(&m_dataLock);

        ScchResult result = ScchCache::load(
            SCCH_DEFAULT_PATH,
            m_frns, m_parent_frns, m_sizes, m_timestamps,
            m_name_offsets, m_attributes, m_string_pool, usnMap
        );

        if (result != ScchResult::Ok) return false;

        for (const auto& [drive, usn] : usnMap) {
            std::wstring wdrive = QString::fromStdString(drive).toStdWString();
            m_next_usns[wdrive] = usn;
            m_drive_list.push_back(wdrive);
        }

        rebuildFrnToIndexMap();
        buildSortedIndices();
        m_isInitialized = true;

        for (const auto& [drive, usn] : m_next_usns) {
            auto* watcher = new UsnWatcher(drive, usn);
            m_watchers.push_back(watcher);
            watcher->start();
        }
    }

    return true;
}

bool MftReader::saveToCache() {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return false;

    std::unordered_map<std::string, uint64_t> usnMap;
    for (const auto& [drive, usn] : m_next_usns)
        usnMap[QString::fromStdWString(drive).toStdString()] = usn;

    return ScchCache::save(
        SCCH_DEFAULT_PATH,
        m_frns, m_parent_frns, m_sizes, m_timestamps,
        m_name_offsets, m_attributes, m_string_pool, usnMap
    );
}

std::wstring MftReader::getPathFast(const std::wstring& volume, uint64_t frn) {
    // 内部加独立锁保护缓存写入，外部依赖读锁访问 SoA
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        auto it = m_path_cache.find(frn);
        if (it != m_path_cache.end()) return it->second;
    }

    std::vector<std::wstring> segments;
    uint64_t currentFrn = frn;
    std::unordered_set<uint64_t> visited;

    while (true) {
        uint64_t maskedFrn = currentFrn & 0x0000FFFFFFFFFFFFull;
        auto idxIt = m_frn_to_idx.find(maskedFrn);
        if (idxIt == m_frn_to_idx.end()) break;
        if (visited.count(maskedFrn)) break;

        visited.insert(maskedFrn);
        uint32_t idx = idxIt->second;

        const char* namePtr = reinterpret_cast<const char*>(
            m_string_pool.data() + m_name_offsets[idx]);
        segments.push_back(QString::fromUtf8(namePtr).toStdWString());

        uint64_t parentFrn = m_parent_frns[idx] & 0x0000FFFFFFFFFFFFull;
        if (parentFrn == 5 || parentFrn == maskedFrn) break;
        currentFrn = parentFrn;
    }

    if (segments.empty()) return L"";

    std::wstring fullPath = volume;
    for (auto itSeg = segments.rbegin(); itSeg != segments.rend(); ++itSeg)
        fullPath += L"\\" + *itSeg;

    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        m_path_cache[frn] = fullPath;
    }
    return fullPath;
}

QString MftReader::getFullPath(int index) const {
    uint64_t frn = 0;
    uint64_t parentFrnRaw = 0;

    {
        QReadLocker lock(&m_dataLock);
        if (index < 0 || index >= (int)m_frns.size()) return QString();
        frn = m_frns[index];
        parentFrnRaw = m_parent_frns[index];
    }

    if (frn == 0) return QString();

    // 从父 FRN 高位提取盘符索引
    uint16_t driveIdx = (uint16_t)(parentFrnRaw >> 48);
    std::wstring volume;
    {
        QReadLocker lock(&m_dataLock);
        volume = (driveIdx < m_drive_list.size()) ? m_drive_list[driveIdx] : L"C:";
    }

    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFast(volume, frn));
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
    return getAttributes(index) & FILE_ATTRIBUTE_DIRECTORY;
}

int MftReader::totalCount() const {
    QReadLocker lock(&m_dataLock);
    return (int)m_frns.size();
}

void MftReader::updateEntryFromUsn(::USN_RECORD_V2* pRecord, const std::wstring& volume) {
    QWriteLocker lock(&m_dataLock);
    
    uint64_t frn = pRecord->FileReferenceNumber;
    uint64_t maskedFrn = frn & 0x0000FFFFFFFFFFFFull;
    uint32_t idx = 0;

    auto it = m_frn_to_idx.find(maskedFrn);
    if (it != m_frn_to_idx.end()) {
        idx = it->second;
    } else {
        idx = (uint32_t)m_frns.size();
        m_frns.push_back(frn);
        m_parent_frns.push_back(0);
        m_sizes.push_back(0);
        m_timestamps.push_back(0);
        m_name_offsets.push_back(0);
        m_attributes.push_back(0);
        m_frn_to_idx[maskedFrn] = idx;
    }

    uint16_t driveIdx = 0;
    auto itDrive = std::find(m_drive_list.begin(), m_drive_list.end(), volume);
    if (itDrive != m_drive_list.end()) {
        driveIdx = (uint16_t)std::distance(m_drive_list.begin(), itDrive);
    } else {
        driveIdx = (uint16_t)m_drive_list.size();
        m_drive_list.push_back(volume);
    }

    m_frns[idx] = frn;
    // 高 16 位存盘符索引
    m_parent_frns[idx] = pRecord->ParentFileReferenceNumber | ((uint64_t)driveIdx << 48);
    m_attributes[idx] = pRecord->FileAttributes;
    m_timestamps[idx] = filetimeToUnixMs(pRecord->TimeStamp.QuadPart);

    std::wstring wname(reinterpret_cast<const wchar_t*>((uint8_t*)pRecord + pRecord->FileNameOffset), pRecord->FileNameLength / 2);
    std::string nameUtf8 = QString::fromStdWString(wname).toUtf8().toStdString();

    m_name_offsets[idx] = (uint32_t)m_string_pool.size();
    m_string_pool.insert(m_string_pool.end(), nameUtf8.begin(), nameUtf8.end());
    m_string_pool.push_back('\0');

    {
        std::lock_guard<std::mutex> cacheLock(m_pathCacheMutex);
        m_path_cache.erase(frn);
        m_path_cache.erase(maskedFrn);
    }
    m_next_usns[volume] = pRecord->Usn + pRecord->RecordLength;

    emit dataChanged();
}

void MftReader::removeEntryByFrn(const std::wstring& volume, uint64_t frn) {
    QWriteLocker lock(&m_dataLock);
    uint64_t maskedFrn = frn & 0x0000FFFFFFFFFFFFull;
    auto it = m_frn_to_idx.find(maskedFrn);
    if (it != m_frn_to_idx.end()) {
        m_frns[it->second] = 0;
        m_frn_to_idx.erase(it);
        {
            std::lock_guard<std::mutex> cacheLock(m_pathCacheMutex);
            m_path_cache.erase(frn);
            m_path_cache.erase(maskedFrn);
        }
        emit dataChanged();
    }
}

QVector<int> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, const QStringList& extensionList, bool includeHidden, bool includeSystem) {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return {};

    std::string queryStr = query.toUtf8().toStdString();
    std::vector<int> results;

    std::vector<size_t> indices;
    indices.reserve(m_frns.size());
    for(size_t i=0; i<m_frns.size(); ++i) if(m_frns[i] != 0) indices.push_back(i);

    std::mutex resMutex;
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
        uint32_t attr = m_attributes[i];
        if (!includeHidden && (attr & FILE_ATTRIBUTE_HIDDEN)) return;
        if (!includeSystem && (attr & FILE_ATTRIBUTE_SYSTEM)) return;

        const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);

        bool match = false;
        if (query.isEmpty()) match = true;
        else if (caseSensitive) match = strstr(name, queryStr.c_str()) != nullptr;
        else match = StrStrIA(name, queryStr.c_str()) != nullptr;

        if (match) {
            std::lock_guard<std::mutex> lk(resMutex);
            results.push_back((int)i);
        }
    });

    return QVector<int>(results.begin(), results.end());
}

QVector<int> MftReader::searchPrefix(const QString& prefix) {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized || m_sorted_indices.empty()) return {};

    std::string prefixStr = prefix.toLower().toStdString();

    auto itLo = std::lower_bound(m_sorted_indices.begin(), m_sorted_indices.end(), prefixStr,
        [this](uint32_t idx, const std::string& p) {
            const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
            return _stricmp(name, p.c_str()) < 0;
        });

    QVector<int> results;
    for (auto it = itLo; it != m_sorted_indices.end(); ++it) {
        const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[*it]);
        if (_strnicmp(name, prefixStr.c_str(), prefixStr.length()) == 0) {
            results.push_back((int)*it);
        } else {
            break;
        }
    }
    return results;
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear();
    m_parent_to_children.clear();
    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            uint64_t maskedFrn = m_frns[i] & 0x0000FFFFFFFFFFFFull;
            m_frn_to_idx[maskedFrn] = (uint32_t)i;

            uint64_t maskedParent = m_parent_frns[i] & 0x0000FFFFFFFFFFFFull;
            m_parent_to_children[maskedParent].push_back(m_frns[i]);
        }
    }
}

void MftReader::buildSortedIndices() {
    m_sorted_indices.clear();
    for(size_t i=0; i<m_frns.size(); ++i) {
        if(m_frns[i] != 0) m_sorted_indices.push_back((uint32_t)i);
    }
    std::sort(std::execution::par, m_sorted_indices.begin(), m_sorted_indices.end(), [this](uint32_t a, uint32_t b){
        const char* s1 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[a]);
        const char* s2 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[b]);
        return _stricmp(s1, s2) < 0;
    });
}

bool MftReader::loadMftDirect(const std::wstring& volumePath, DriveResult& result) {
    std::wstring devicePath = L"\\\\.\\" + volumePath.substr(0, 2);
    HANDLE hVolume = CreateFileW(devicePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hVolume == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA_V0 journalData;
    DWORD cb;
    if (!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &cb, NULL)) {
        CloseHandle(hVolume);
        return false;
    }

    result.volume = volumePath.substr(0, 2);
    m_next_usns[result.volume] = journalData.NextUsn;

    MFT_ENUM_DATA_V0 enumData = {0};
    enumData.HighUsn = journalData.NextUsn;

    std::vector<uint8_t> buffer(2 * 1024 * 1024); // 2MB
    result.entries.reserve(1000000);

    while (DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer.data(), (DWORD)buffer.size(), &cb, NULL)) {
        if (cb < 8) break;
        uint8_t* pRecord = buffer.data() + 8;
        uint8_t* pEnd = buffer.data() + cb;
        while (pRecord < pEnd) {
            USN_RECORD_V2* record = (USN_RECORD_V2*)pRecord;
            RawEntry entry;
            entry.frn = record->FileReferenceNumber;
            entry.parentFrn = record->ParentFileReferenceNumber;
            entry.attributes = record->FileAttributes;
            entry.modifyTime = filetimeToUnixMs(record->TimeStamp.QuadPart);
            entry.size = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 4096;

            std::wstring wname(record->FileName, record->FileNameLength / 2);
            entry.nameUtf8 = QString::fromStdWString(wname).toUtf8().toStdString();

            result.entries.push_back(std::move(entry));
            pRecord += record->RecordLength;
        }
        enumData.StartFileReferenceNumber = *(DWORDLONG*)buffer.data();
    }
    CloseHandle(hVolume);
    return !result.entries.empty();
}

void MftReader::mergeDriveResult(const DriveResult& result) {
    size_t oldSize = m_frns.size();
    size_t addSize = result.entries.size();

    m_drive_list.push_back(result.volume);
    uint16_t driveIdx = (uint16_t)(m_drive_list.size() - 1);

    m_frns.resize(oldSize + addSize);
    m_parent_frns.resize(oldSize + addSize);
    m_sizes.resize(oldSize + addSize);
    m_timestamps.resize(oldSize + addSize);
    m_name_offsets.resize(oldSize + addSize);
    m_attributes.resize(oldSize + addSize);

    size_t totalStringDelta = 0;
    for (const auto& entry : result.entries) totalStringDelta += entry.nameUtf8.size() + 1;
    m_string_pool.reserve(m_string_pool.size() + totalStringDelta);

    uint32_t currentOffset = (uint32_t)m_string_pool.size();
    for (size_t i = 0; i < addSize; ++i) {
        size_t idx = oldSize + i;
        const auto& entry = result.entries[i];
        m_frns[idx] = entry.frn;
        // 高 16 位存盘符索引
        m_parent_frns[idx] = entry.parentFrn | ((uint64_t)driveIdx << 48);
        m_sizes[idx] = entry.size;
        m_timestamps[idx] = entry.modifyTime;
        m_attributes[idx] = entry.attributes;
        m_name_offsets[idx] = currentOffset;
        m_string_pool.insert(m_string_pool.end(), entry.nameUtf8.begin(), entry.nameUtf8.end());
        m_string_pool.push_back('\0');
        currentOffset += (uint32_t)entry.nameUtf8.size() + 1;
    }
}

QStringList MftReader::getAvailableDrives() const {
    QStringList drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1 << i)) {
            QString d = QString(QChar('A' + i)) + ":\\";
            drives << d;
        }
    }
    return drives;
}

bool MftReader::isFixedDrive(const QString& drive) const {
    return GetDriveTypeW(drive.toStdWString().c_str()) == DRIVE_FIXED;
}

} // namespace ArcMeta
