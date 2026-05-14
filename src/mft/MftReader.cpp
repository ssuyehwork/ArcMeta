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
    if (filetime <= 0) return 0;
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
    }
    for (auto* w : toStop) {
        if (w) { w->stop(); delete w; }
    }
    QWriteLocker lock(&m_dataLock);
    clearInternal();
}

void MftReader::buildIndex(const QStringList& drives) {
    clear();
    QWriteLocker lock(&m_dataLock);
    
    QStringList targetDrives = drives;
    if (targetDrives.isEmpty()) {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (1 << i)) {
                QString root = QString(QChar('A' + i)) + ":\\";
                if (GetDriveTypeW(reinterpret_cast<const wchar_t*>(root.utf16())) == DRIVE_FIXED)
                    targetDrives << root.left(2);
            }
        }
    }

    for (const QString& driveStr : targetDrives) {
        std::wstring volume = driveStr.toStdWString();
        if (volume.size() > 2 && volume.back() == L'\\') volume.pop_back();

        DriveResult result;
        if (loadMftDirect(volume, result)) {
            size_t driveIdx = m_drive_list.size();
            m_drive_list.push_back(volume);
            m_next_usns[volume] = result.nextUsn;
            mergeDriveResult(volume, result, driveIdx);

            // 扫描完一个盘立即保存
            saveDriveToCache(driveIdx);
        }
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    for (const auto& [volume, usn] : m_next_usns) {
        auto* watcher = new UsnWatcher(volume, usn);
        m_watchers.push_back(watcher);
        watcher->start();
    }
}

bool MftReader::loadFromCache() {
    clearInternal();

    std::filesystem::path cacheDir = "ArcMeta/cache";
    if (!std::filesystem::exists(cacheDir) || !std::filesystem::is_directory(cacheDir)) return false;

    QWriteLocker lock(&m_dataLock);
    for (auto const& dir_entry : std::filesystem::directory_iterator{cacheDir}) {
        if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".scch") {
            std::vector<uint64_t>  f, pf;
            std::vector<int64_t>   s, t;
            std::vector<uint32_t>  no, attr;
            std::vector<uint8_t>   sp;
            std::unordered_map<std::string, uint64_t> usnMap;

            ScchResult res = ScchCache::load(dir_entry.path().string().c_str(), f, pf, s, t, no, attr, sp, usnMap);
            if (res == ScchResult::Ok && !usnMap.empty()) {
                // 物理对标：获取该缓存对应的正确盘符索引，防止多盘合并时的索引错位
                std::wstring wDrive = QString::fromStdString(usnMap.begin()->first).toStdWString();
                size_t driveIdx = 0;
                auto it = std::find(m_drive_list.begin(), m_drive_list.end(), wDrive);
                if (it == m_drive_list.end()) {
                    driveIdx = m_drive_list.size();
                    m_drive_list.push_back(wDrive);
                } else {
                    driveIdx = std::distance(m_drive_list.begin(), it);
                }
                m_next_usns[wDrive] = usnMap.begin()->second;

                size_t oldPoolSize = m_string_pool.size();
                size_t count = f.size();

                // 合并 SoA 数据并执行 48 位 FRN 编码，彻底杜绝跨盘符 FRN 冲突
                for (size_t i = 0; i < count; ++i) {
                    uint64_t encodedFrn = (static_cast<uint64_t>(driveIdx) << 48) | (f[i] & 0x0000FFFFFFFFFFFFull);
                    uint64_t encodedPf = (static_cast<uint64_t>(driveIdx) << 48) | (pf[i] & 0x0000FFFFFFFFFFFFull);

                    m_frns.push_back(encodedFrn);
                    m_parent_frns.push_back(encodedPf);
                    m_sizes.push_back(s[i]);
                    m_timestamps.push_back(t[i]);
                    m_attributes.push_back(attr[i]);
                    m_name_offsets.push_back(no[i] + (uint32_t)oldPoolSize);
                }
                m_string_pool.insert(m_string_pool.end(), sp.begin(), sp.end());
            }
        }
    }

    if (m_frns.empty()) return false;

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    for (const auto& [volume, usn] : m_next_usns) {
        auto* watcher = new UsnWatcher(volume, usn);
        m_watchers.push_back(watcher);
        watcher->start();
    }
    return true;
}

bool MftReader::saveToCache() {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return false;
    for (size_t i = 0; i < m_drive_list.size(); ++i) {
        saveDriveToCache(i);
    }
    return true;
}

bool MftReader::saveDriveToCache(size_t driveIdx) {
    if (driveIdx >= m_drive_list.size()) return false;
    std::wstring volume = m_drive_list[driveIdx];

    std::vector<uint64_t>  f, pf;
    std::vector<int64_t>   s, t;
    std::vector<uint32_t>  no, attr;
    std::vector<uint8_t>   sp;
    std::unordered_map<uint32_t, uint32_t> offsetMap;

    for (size_t i = 0; i < m_frns.size(); ++i) {
        if ((m_parent_frns[i] >> 48) == driveIdx) {
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
    const char* ptr = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[index]);
    return QString::fromUtf8(ptr);
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

        uint64_t drivePart = m_parent_frns[idx] & 0xFFFF000000000000ull;
        uint64_t parentIndex = m_parent_frns[idx] & 0x0000FFFFFFFFFFFFull;

        // 物理规则：FRN 5 是 NTFS 根目录。到达根目录或父级无效时停止。
        if (parentIndex == 5 || parentIndex == (cur & 0x0000FFFFFFFFFFFFull) || parentIndex == 0) break;

        // 维持带盘符编码的 FRN 进行下一轮查找
        cur = drivePart | parentIndex;
    }
    if (segments.empty()) return L"";
    std::wstring path = volume;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) path += L"\\" + *it;
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        m_path_cache[frn] = path;
    }
    return path;
}

QVector<int> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, 
                              const QStringList& extensionList, bool includeHidden, bool includeSystem) {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return {};
    std::vector<int> res;
    QRegularExpression re;
    if (useRegex && !query.isEmpty())
        re = QRegularExpression(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);

    std::mutex mtx;
    std::vector<int> indices(m_frns.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](int i) {
        if (m_frns[i] == 0) return;
        uint32_t at = m_attributes[i];
        if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) return;
        if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) return;
        const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
        QString name = QString::fromUtf8(p);
        if (!extensionList.isEmpty()) {
            bool ok = false;
            for (const QString& ex : extensionList) { if (name.endsWith("." + ex, Qt::CaseInsensitive)) { ok = true; break; } }
            if (!ok) return;
        }
        bool m = false;
        if (query.isEmpty()) m = true;
        else if (useRegex) m = re.match(name).hasMatch();
        else m = name.contains(query, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);

        if (m) { std::lock_guard<std::mutex> l(mtx); res.push_back(i); }
    });
    return QVector<int>(res.begin(), res.end());
}

void MftReader::updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume) {
    QWriteLocker lock(&m_dataLock);
    uint64_t frn = record->FileReferenceNumber;
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(
        reinterpret_cast<uint8_t*>(record) + record->FileNameOffset), record->FileNameLength / 2);

    size_t dIdx = 0;
    bool found = false;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; found = true; break; } }
    if (!found) return; // 未知盘符变动，不予处理

    uint64_t encodedFrn = (static_cast<uint64_t>(dIdx) << 48) | (frn & 0x0000FFFFFFFFFFFFull);
    uint64_t encodedPf = (static_cast<uint64_t>(dIdx) << 48) | (record->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull);

    auto it = m_frn_to_idx.find(encodedFrn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_frns[idx] = encodedFrn; // 冗余确认
        m_parent_frns[idx] = encodedPf;
        m_attributes[idx] = record->FileAttributes;
        m_timestamps[idx] = filetimeToUnixMs(record->TimeStamp.QuadPart);
        QByteArray utf8 = name.toUtf8();
        m_name_offsets[idx] = (uint32_t)m_string_pool.size();
        m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
        m_string_pool.push_back('\0');
    } else {
        uint32_t newIdx = (uint32_t)m_frns.size();
        m_frns.push_back(frn);
        m_parent_frns.push_back(encodedPf);
        m_sizes.push_back(0);
        m_timestamps.push_back(filetimeToUnixMs(record->TimeStamp.QuadPart));
        m_attributes.push_back(record->FileAttributes);
        QByteArray utf8 = name.toUtf8();
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
        m_string_pool.push_back('\0');
        m_frn_to_idx[encodedFrn] = newIdx;
    }
    { std::lock_guard<std::mutex> l(m_pathCacheMutex); m_path_cache.erase(encodedFrn); }
    m_next_usns[volume] = record->Usn;
    m_dirty_count++;
    if (m_dirty_count >= 1000) { m_dirty_count = 0; saveDriveToCache(dIdx); }
    emit dataChanged();
}

void MftReader::removeEntryByFrn(const std::wstring& volume, uint64_t frn) {
    QWriteLocker lock(&m_dataLock);
    size_t dIdx = 0;
    bool found = false;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; found = true; break; } }
    if (!found) return;

    uint64_t encodedFrn = (static_cast<uint64_t>(dIdx) << 48) | (frn & 0x0000FFFFFFFFFFFFull);

    auto it = m_frn_to_idx.find(encodedFrn);
    if (it != m_frn_to_idx.end()) {
        m_frns[it->second] = 0;
        m_frn_to_idx.erase(it);
        { std::lock_guard<std::mutex> l(m_pathCacheMutex); m_path_cache.erase(encodedFrn); }
        emit dataChanged();
    }
}

bool MftReader::loadMftDirect(const std::wstring& volume, DriveResult& result) {
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
            RawEntry e; e.frn = rec->FileReferenceNumber; e.parentFrn = rec->ParentFileReferenceNumber;
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

void MftReader::mergeDriveResult(const std::wstring& volume, const DriveResult& result, size_t driveIdx) {
    Q_UNUSED(volume);
    size_t count = result.entries.size();
    m_frns.reserve(m_frns.size() + count);
    m_parent_frns.reserve(m_parent_frns.size() + count);
    m_sizes.reserve(m_sizes.size() + count);
    m_timestamps.reserve(m_timestamps.size() + count);
    m_name_offsets.reserve(m_name_offsets.size() + count);
    m_attributes.reserve(m_attributes.size() + count);

    for (const auto& e : result.entries) {
        // 物理补丁：在 SoA 核心数组中直接存储带盘符编码的 FRN，确保全局唯一性
        uint64_t encodedFrn = (static_cast<uint64_t>(driveIdx) << 48) | (e.frn & 0x0000FFFFFFFFFFFFull);
        uint64_t encodedPf = (static_cast<uint64_t>(driveIdx) << 48) | (e.parentFrn & 0x0000FFFFFFFFFFFFull);

        m_frns.push_back(encodedFrn);
        m_parent_frns.push_back(encodedPf);
        m_sizes.push_back(0);
        m_timestamps.push_back(e.modifyTime);
        m_attributes.push_back(e.attributes);

        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), e.nameUtf8.begin(), e.nameUtf8.end());
        m_string_pool.push_back('\0');
    }
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear();
    m_parent_to_children.clear();
    for (size_t i = 0; i < m_frns.size(); ++i) {
        uint64_t encodedFrn = m_frns[i];
        if (encodedFrn != 0) {
            m_frn_to_idx[encodedFrn] = (uint32_t)i;

            // 物理对标：父子关系映射也必须携带盘符编码，防止跨盘符 FRN 冲突
            uint64_t encodedPf = m_parent_frns[i];
            uint64_t parentFrnOnly = encodedPf & 0x0000FFFFFFFFFFFFull;
            if (parentFrnOnly != 0 && parentFrnOnly != 5) {
                m_parent_to_children[encodedPf].push_back(encodedFrn);
            }
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
