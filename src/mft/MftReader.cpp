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
    // 2026-05-14 物理对标修复点 P2：将耗时扫描逻辑移出写锁，仅在合并数据时持锁
    // 1. 确定目标盘符 (锁外)
    QStringList targetDrives = drives;
    if (targetDrives.isEmpty()) {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (1 << i)) {
                QString root = QString(QChar('A' + i)) + ":\\";
                // 对标 Rust 版：支持 USB 盘扫描
                UINT type = GetDriveTypeW(reinterpret_cast<const wchar_t*>(root.utf16()));
                if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE)
                    targetDrives << root.left(2);
            }
        }
    }

    // 2. 执行并发磁盘 I/O 扫描 (锁外)
    struct ScannedDrive {
        std::wstring volume;
        DriveResult result;
        bool success = false;
    };
    std::vector<ScannedDrive> scannedResults(targetDrives.size());
    
    std::vector<int> driveIndices(targetDrives.size());
    std::iota(driveIndices.begin(), driveIndices.end(), 0);
    
    // 使用并行算法加速多盘扫描，杜绝单盘挂起导致全盘阻塞
    std::for_each(std::execution::par, driveIndices.begin(), driveIndices.end(), [&](int i) {
        std::wstring volume = targetDrives[i].toStdWString();
        if (volume.size() > 2 && volume.back() == L'\\') volume.pop_back();
        
        scannedResults[i].volume = volume;
        scannedResults[i].success = loadMftDirect(volume, scannedResults[i].result);
    });

    // 3. 进入原子写锁周期进行内存合并 (此时已无耗时 I/O)
    QWriteLocker lock(&m_dataLock);
    
    // 停止并清理旧监控器
    std::vector<UsnWatcher*> toStop = std::move(m_watchers);
    m_watchers.clear();
    // 临时释放锁以停止线程，防止 MftReader 对象生命周期竞争导致的阻塞
    lock.unlock();
    for (auto* w : toStop) { if (w) { w->stop(); delete w; } }
    lock.relock();

    clearInternal();

    for (auto& sr : scannedResults) {
        if (!sr.success || sr.result.entries.empty()) continue;
        
        size_t driveIdx = m_drive_list.size();
        m_drive_list.push_back(sr.volume);
        m_next_usns[sr.volume] = sr.result.nextUsn;
        mergeDriveResult(sr.volume, sr.result, driveIdx);
        
        // 2026-05-14 修复：由于当前已持锁，必须调用 Internal 版本，防止死锁
        saveDriveToCacheInternal(driveIdx);
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    for (const auto& [volume, usn] : m_next_usns) {
        auto* watcher = new UsnWatcher(volume, usn, nullptr);
        m_watchers.push_back(watcher);
    }

    // 2026-05-14 修复：锁释放后再启动线程
    lock.unlock();
    for (auto* watcher : m_watchers) {
        watcher->start();
    }
}

bool MftReader::loadFromCache() {
    // 2026-05-14 修复点 2：修复数据竞争 (Data Race)。
    // 物理上必须先拿锁，再清空容器，杜绝查询线程访问到已清空的碎片数据。
    std::filesystem::path cacheDir = "ArcMeta/cache";
    if (!std::filesystem::exists(cacheDir)) return false;

    QWriteLocker lock(&m_dataLock);
    clearInternal();

    for (auto const& dir_entry : std::filesystem::directory_iterator{cacheDir}) {
        if (dir_entry.path().extension() == ".scch") {
            std::vector<uint64_t>  f, pf;
            std::vector<int64_t>   s, t;
            std::vector<uint32_t>  no, attr;
            std::vector<uint8_t>   sp;
            std::unordered_map<std::string, uint64_t> usnMap;

            ScchResult res = ScchCache::load(dir_entry.path().string().c_str(), f, pf, s, t, no, attr, sp, usnMap);
            if (res == ScchResult::Ok) {
                size_t driveIdx = m_drive_list.size();
                size_t count = f.size();
                size_t oldPoolSize = m_string_pool.size();

                // 2026-05-14 物理对标修复点 P4：确保 SoA 数组长度在合并过程中严格对齐。
                // 预留空间，防止循环中频繁重分配
                m_frns.reserve(m_frns.size() + count);
                m_sizes.reserve(m_sizes.size() + count);
                m_timestamps.reserve(m_timestamps.size() + count);
                m_attributes.reserve(m_attributes.size() + count);
                m_parent_frns.reserve(m_parent_frns.size() + count);
                m_name_offsets.reserve(m_name_offsets.size() + count);

                // 同步填充，确保各数组在任意时刻的 size 偏差最小（虽然已持锁，但这符合 SoA 鲁棒性规范）
                for (size_t i = 0; i < count; ++i) {
                    m_frns.push_back(f[i]);
                    m_sizes.push_back(s[i]);
                    m_timestamps.push_back(t[i]);
                    m_attributes.push_back(attr[i]);
                    
                    uint64_t encodedPf = (static_cast<uint64_t>(driveIdx) << 48) | (pf[i] & 0x0000FFFFFFFFFFFFull);
                    m_parent_frns.push_back(encodedPf);
                    m_name_offsets.push_back(no[i] + (uint32_t)oldPoolSize);
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

    for (const auto& [volume, usn] : m_next_usns) {
        // 2026-05-14 修复点 1：杜绝跨线程父对象绑定
        auto* watcher = new UsnWatcher(volume, usn, nullptr);
        m_watchers.push_back(watcher);
    }

    // 2026-05-14 修复：锁释放后再启动线程
    lock.unlock();
    for (auto* watcher : m_watchers) {
        watcher->start();
    }
    return true;
}

bool MftReader::saveToCache() {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return false;
    for (size_t i = 0; i < m_drive_list.size(); ++i) {
        saveDriveToCacheInternal(i);
    }
    return true;
}

bool MftReader::saveDriveToCache(size_t driveIdx) {
    QReadLocker lock(&m_dataLock);
    return saveDriveToCacheInternal(driveIdx);
}

bool MftReader::saveDriveToCacheInternal(size_t driveIdx) {
    if (driveIdx >= m_drive_list.size()) return false;
    std::wstring volume = m_drive_list[driveIdx];
    
    std::vector<uint64_t>  f, pf;
    std::vector<int64_t>   s, t;
    std::vector<uint32_t>  no, attr;
    std::vector<uint8_t>   sp;
    std::unordered_map<uint32_t, uint32_t> offsetMap;

    for (size_t i = 0; i < m_frns.size(); ++i) {
        // FRN 为 0 表示该项已被逻辑删除，不保存
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
        uint64_t parent = m_parent_frns[idx] & 0x0000FFFFFFFFFFFFull;
        if (parent == 5 || parent == cur || parent == 0) break;
        cur = parent;
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

    bool hasQuery = !query.isEmpty();
    bool hasExt = !extensionList.isEmpty();
    QRegularExpression re;
    if (useRegex && hasQuery) {
        re = QRegularExpression(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
    }

    // 预处理扩展名，确保兼容 ".txt" 和 "txt"
    QStringList normalizedExts;
    if (hasExt) {
        for (const QString& ex : extensionList) {
            normalizedExts << (ex.startsWith('.') ? ex : "." + ex);
        }
    }

    // 2026-05-14 深度优化：采用分块归并模式，大幅降低锁竞争与内存分配频率
    std::mutex mtx;
    std::vector<int> finalRes;
    finalRes.reserve(m_frns.size() / 16);

    size_t total = m_frns.size();
    const size_t grainSize = 4096; // 每块处理 4096 条，平衡并行度与锁竞争
    size_t numChunks = (total + grainSize - 1) / grainSize;
    std::vector<size_t> chunkIndices(numChunks);
    std::iota(chunkIndices.begin(), chunkIndices.end(), 0);

    std::for_each(std::execution::par, chunkIndices.begin(), chunkIndices.end(), [&](size_t chunkIdx) {
        std::vector<int> localRes;
        localRes.reserve(grainSize / 8);

        size_t start = chunkIdx * grainSize;
        size_t end = std::min(start + grainSize, total);

        for (size_t i = start; i < end; ++i) {
            if (m_frns[i] == 0) continue;

            // 1. 属性过滤 (比特位操作，极快)
            uint32_t at = m_attributes[i];
            if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;

            const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);

            // 2. 字符串匹配 (按需延迟转换)
            if (!hasQuery && !hasExt) {
                localRes.push_back((int)i);
                continue;
            }

            QString name = QString::fromUtf8(p);

            if (hasExt) {
                bool extMatch = false;
                for (const QString& ex : normalizedExts) {
                    if (name.endsWith(ex, Qt::CaseInsensitive)) { extMatch = true; break; }
                }
                if (!extMatch) continue;
            }

            if (!hasQuery) {
                localRes.push_back((int)i);
            } else {
                bool match = useRegex ? re.match(name).hasMatch()
                                     : name.contains(query, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
                if (match) localRes.push_back((int)i);
            }
        }

        if (!localRes.empty()) {
            std::lock_guard<std::mutex> l(mtx);
            finalRes.insert(finalRes.end(), localRes.begin(), localRes.end());
        }
    });

    return QVector<int>::fromStdVector(finalRes);
}

void MftReader::updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume) {
    QWriteLocker lock(&m_dataLock);
    uint64_t frn = record->FileReferenceNumber;
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(
        reinterpret_cast<uint8_t*>(record) + record->FileNameOffset), record->FileNameLength / 2);
    
    size_t dIdx = 0;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; break; } }

    uint64_t encodedPf = (static_cast<uint64_t>(dIdx) << 48) | (record->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull);
    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_parent_frns[idx] = encodedPf;
        m_attributes[idx] = record->FileAttributes;
        m_timestamps[idx] = filetimeToUnixMs(record->TimeStamp.QuadPart);

        QByteArray utf8 = name.toUtf8();
        uint32_t oldOff = m_name_offsets[idx];

        // 计算旧字符串的容量（含 '\0'）
        const char* oldPtr = reinterpret_cast<const char*>(m_string_pool.data() + oldOff);
        size_t oldLen = strlen(oldPtr); // 不含 '\0'

        if ((size_t)utf8.size() <= oldLen) {
            // 新名称不超过旧名称长度：原地覆写，零额外分配
            memcpy(m_string_pool.data() + oldOff, utf8.constData(), utf8.size());
            m_string_pool[oldOff + utf8.size()] = '\0';
            // m_name_offsets[idx] 保持不变
        } else {
            // 新名称更长：追加到池尾，旧空间标记为废弃（靠定期 saveToCache 清理）
            m_name_offsets[idx] = (uint32_t)m_string_pool.size();
            m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
            m_string_pool.push_back('\0');
        }
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
        m_frns[it->second] = 0;
        m_frn_to_idx.erase(it);
        { std::lock_guard<std::mutex> l(m_pathCacheMutex); m_path_cache.erase(frn); }
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
        m_frns.push_back(e.frn);
        m_parent_frns.push_back((static_cast<uint64_t>(driveIdx) << 48) | (e.parentFrn & 0x0000FFFFFFFFFFFFull));
        m_sizes.push_back(0); m_timestamps.push_back(e.modifyTime); m_attributes.push_back(e.attributes);
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
