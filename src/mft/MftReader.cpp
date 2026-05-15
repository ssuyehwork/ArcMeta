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
#include <QFileIconProvider>
#include <QFileInfo>

namespace ArcMeta {

static int64_t filetimeToUnixMs(int64_t filetime) {
    // 2026-05-14 物理对标 Windows FILETIME 标准 (1601 Epoch to 1970 Unix)
    // 116444736000000000LL 是 1601 到 1970 的 100纳秒数
    if (filetime < 116444736000000000LL) return 0;
    // 10000LL 将 100纳秒 转换为 毫秒 (1ms = 10,000 * 100ns)
    return (filetime - 116444736000000000LL) / 10000LL;
}

static bool enablePrivilege(LPCWSTR privilege) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    LUID luid;
    if (!LookupPrivilegeValue(NULL, privilege, &luid)) { CloseHandle(hToken); return false; }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) { CloseHandle(hToken); return false; }
    bool ok = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return ok;
}

MftReader& MftReader::instance() {
    static MftReader inst;
    static std::once_flag flag;
    std::call_once(flag, []() {
        enablePrivilege(SE_BACKUP_NAME);
        enablePrivilege(SE_RESTORE_NAME);
    });
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
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        m_path_cache.clear();
    }
    {
        QWriteLocker lock(&m_iconCacheLock);
        m_icon_cache.clear();
    }
    m_next_usns.clear();
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

            // 2026-05-14 交互优化：对齐 Rust 原版逻辑 (空即是无)
            // 如果用户没有输入任何搜索词或后缀，则不显示任何结果。
            if (!hasQuery && !hasExt) continue;

            const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
            
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
    USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(record);
    uint64_t frn, parentFrn;
    uint32_t attr;
    LARGE_INTEGER timestamp;
    WORD fileNameLength, fileNameOffset;

    if (header->MajorVersion == 2) {
        frn = record->FileReferenceNumber;
        parentFrn = record->ParentFileReferenceNumber;
        attr = record->FileAttributes;
        timestamp = record->TimeStamp;
        fileNameLength = record->FileNameLength;
        fileNameOffset = record->FileNameOffset;
    } else if (header->MajorVersion == 3) {
        USN_RECORD_V3* v3 = reinterpret_cast<USN_RECORD_V3*>(record);
        frn = *reinterpret_cast<uint64_t*>(&v3->FileReferenceNumber);
        parentFrn = *reinterpret_cast<uint64_t*>(&v3->ParentFileReferenceNumber);
        attr = v3->FileAttributes;
        timestamp = v3->TimeStamp;
        fileNameLength = v3->FileNameLength;
        fileNameOffset = v3->FileNameOffset;
    } else return;

    uint64_t fileSize = 0;
    int64_t finalModifyTime = filetimeToUnixMs(timestamp.QuadPart);
    uint32_t finalAttr = attr;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring rootPath = volume + L"\\";
        HANDLE hHint = CreateFileW(rootPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hHint != INVALID_HANDLE_VALUE) {
            // 2026-05-14 兼容性修正：统一使用 64 位 FileIdType 以适配旧版 Windows SDK
            FILE_ID_DESCRIPTOR id = {0};
            id.dwSize = sizeof(FILE_ID_DESCRIPTOR);
            id.Type = FileIdType;
            id.FileId.QuadPart = frn;
            HANDLE hFile = OpenFileById(hHint, &id, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
            if (hFile != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION bhfi;
                if (GetFileInformationByHandle(hFile, &bhfi)) {
                    fileSize = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                    finalAttr = bhfi.dwFileAttributes;
                    finalModifyTime = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                }
                CloseHandle(hFile);
            }
            CloseHandle(hHint);
        }
    }

    QWriteLocker lock(&m_dataLock);
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(reinterpret_cast<uint8_t*>(record) + fileNameOffset), fileNameLength / 2);
    size_t dIdx = 0;
    for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; break; } }
    uint64_t encodedPf = (static_cast<uint64_t>(dIdx) << 48) | (parentFrn & 0x0000FFFFFFFFFFFFull);
    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_parent_frns[idx] = encodedPf;
        m_attributes[idx] = finalAttr;
        
        // 2026-05-14 逻辑加固：仅在获取到有效物理属性时才更新，避免 API 失败导致的 USN 数据被默认 0 值覆盖
        if (fileSize > 0) m_sizes[idx] = fileSize;
        if (finalModifyTime > 0) m_timestamps[idx] = finalModifyTime;

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
        m_timestamps.push_back(finalModifyTime);
        m_attributes.push_back(finalAttr);
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
}

bool MftReader::loadMftDirect(const std::wstring& volume, MftReader::DriveResult& result) {
    std::wstring dev = L"\\\\.\\" + volume;
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 2026-05-14 获取根目录句柄作为 Hint，这对于 OpenFileById 的稳定性至关重要
    std::wstring rootPath = volume + L"\\";
    HANDLE hHint = CreateFileW(rootPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    USN_JOURNAL_DATA_V0 j; DWORD cb;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &j, sizeof(j), &cb, NULL)) { 
        if (hHint != INVALID_HANDLE_VALUE) CloseHandle(hHint);
        CloseHandle(h); return false; 
    }
    result.nextUsn = j.NextUsn;
    MFT_ENUM_DATA_V0 ed = {0}; ed.HighUsn = j.NextUsn;
    std::vector<uint8_t> buf(1024 * 1024);
    while (DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &ed, sizeof(ed), buf.data(), (DWORD)buf.size(), &cb, NULL)) {
        if (cb < 8) break;
        uint8_t* p = buf.data() + 8; uint8_t* end = buf.data() + cb;
        while (p < end) {
            USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(p);
            uint64_t frn, parentFrn;
            LARGE_INTEGER timestamp;
            uint32_t attr;
            WORD fileNameLength, fileNameOffset;

            if (header->MajorVersion == 2) {
                USN_RECORD_V2* rec = reinterpret_cast<USN_RECORD_V2*>(p);
                frn = rec->FileReferenceNumber;
                parentFrn = rec->ParentFileReferenceNumber;
                timestamp = rec->TimeStamp;
                attr = rec->FileAttributes;
                fileNameLength = rec->FileNameLength;
                fileNameOffset = rec->FileNameOffset;
            } else if (header->MajorVersion == 3) {
                USN_RECORD_V3* rec = reinterpret_cast<USN_RECORD_V3*>(p);
                frn = *reinterpret_cast<uint64_t*>(&rec->FileReferenceNumber);
                parentFrn = *reinterpret_cast<uint64_t*>(&rec->ParentFileReferenceNumber);
                timestamp = rec->TimeStamp;
                attr = rec->FileAttributes;
                fileNameLength = rec->FileNameLength;
                fileNameOffset = rec->FileNameOffset;
            } else {
                p += header->RecordLength;
                continue;
            }

            MftReader::RawEntry e; 
            e.frn = frn; 
            e.parentFrn = parentFrn;
            e.size = 0;
            e.attributes = attr;
            e.modifyTime = filetimeToUnixMs(timestamp.QuadPart);

            if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                // 2026-05-14 兼容性修正：统一使用 64 位 FileIdType 以适配旧版 Windows SDK
                FILE_ID_DESCRIPTOR id = {0};
                id.dwSize = sizeof(FILE_ID_DESCRIPTOR);
                id.Type = FileIdType;
                id.FileId.QuadPart = frn;

                // 2026-05-14 工业级属性获取：以 0 访问权限打开句柄，配合 Hint 句柄，极大提升成功率
                HANDLE hFile = OpenFileById(hHint != INVALID_HANDLE_VALUE ? hHint : h, &id, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
                if (hFile != INVALID_HANDLE_VALUE) {
                    BY_HANDLE_FILE_INFORMATION bhfi;
                    if (GetFileInformationByHandle(hFile, &bhfi)) {
                        e.size = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                        e.attributes = bhfi.dwFileAttributes;
                        e.modifyTime = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                    }
                    CloseHandle(hFile);
                }
            }
            QString n = QString::fromUtf16(reinterpret_cast<const char16_t*>(p + fileNameOffset), fileNameLength / 2);
            e.nameUtf8 = n.toUtf8().toStdString();
            result.entries.push_back(std::move(e));
            p += header->RecordLength;
        }
        ed.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buf.data());
    }
    if (hHint != INVALID_HANDLE_VALUE) CloseHandle(hHint);
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
    m_frn_to_idx.clear();
    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            m_frn_to_idx[m_frns[i]] = (uint32_t)i;
        }
    }
}

QIcon MftReader::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    {
        QReadLocker lock(&m_iconCacheLock);
        auto it = m_icon_cache.find(key);
        if (it != m_icon_cache.end()) return *it;
    }

    QFileIconProvider provider;
    QIcon icon;
    if (isDir) {
        icon = provider.icon(QFileIconProvider::Folder);
    } else {
        if (key.length() > 12) key = "unknown";
        icon = provider.icon(QFileInfo("dummy." + key));
        if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
    }

    {
        QWriteLocker lock(&m_iconCacheLock);
        m_icon_cache[key] = icon;
    }
    return icon;
}

} // namespace ArcMeta
