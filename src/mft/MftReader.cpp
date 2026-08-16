#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MftReader.h"
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
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent>
#include <QFuture>
#include <QFileIconProvider>
#include <QFileInfo>
#include <queue>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef run
#undef run
#endif


namespace ArcMeta {

static int64_t filetimeToUnixMs(int64_t filetime) {
    // 2026-05-14 物理对标 Windows FILETIME 标准 (1601 Epoch to 1970 Unix)
    // 116444736000000000LL 是 1601 到 1970 的 100纳秒数
    // 如果时间戳小于 1970 或等于 0，则返回 0 以便 UI 能够正确忽略或显示占位符
    if (filetime <= 116444736000000000LL) return 0;
    // 10000LL 将 100纳秒 转换为 毫秒 (1ms = 10,000 * 100ns)
    return (filetime - 116444736000000000LL) / 10000LL;
}

static bool enablePrivilege(LPCWSTR privilege) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        qDebug() << "[MftReader] OpenProcessToken 失败，无法提升权限:" << QString::fromWCharArray(privilege);
        return false;
    }
    LUID luid;
    if (!LookupPrivilegeValue(NULL, privilege, &luid)) {
        qDebug() << "[MftReader] LookupPrivilegeValue 失败:" << QString::fromWCharArray(privilege);
        CloseHandle(hToken);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        qDebug() << "[MftReader] AdjustTokenPrivileges 失败:" << QString::fromWCharArray(privilege);
        CloseHandle(hToken);
        return false;
    }
    bool ok = (GetLastError() == ERROR_SUCCESS);
    if (!ok) {
        qDebug() << "[MftReader] 权限提升失败 (可能由于非管理员运行):" << QString::fromWCharArray(privilege);
    } else {
        qDebug() << "[MftReader] 权限提升成功:" << QString::fromWCharArray(privilege);
    }
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
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        m_metadata_queue.clear();
    }
    // 极致工业级优化方案 A：物理内存“强制归还”
    // 使用 Swap 技巧强制 STL 释放 Capacity 并归还堆内存给操作系统
    std::vector<uint64_t>().swap(m_frns);
    std::vector<uint64_t>().swap(m_parent_frns);
    std::vector<int64_t>().swap(m_sizes);
    std::vector<int64_t>().swap(m_timestamps);
    std::vector<uint32_t>().swap(m_name_offsets);
    std::vector<uint32_t>().swap(m_attributes);
    std::vector<uint8_t>().swap(m_metadata_fetched);
    std::vector<uint8_t>().swap(m_string_pool);
    std::vector<uint32_t>().swap(m_sorted_indices);

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
    m_isInitialized = false;
    for (int i = 0; i < 32; ++i) {
        m_drive_dirty_counts[i] = 0;
        m_drive_entry_indices[i].clear();
    }
}

void MftReader::clear() {
    // 极致工业级重构：非阻塞异步清理链
    // 1. 立即标记状态失效，让 UI 线程在 request_lock 时能快速感知并退出，实现“秒关”体验
    {
        QWriteLocker lock(&m_dataLock);
        if (!m_isInitialized || m_is_clearing.load()) return;
        m_is_clearing.store(true);
        m_abort_scan.store(true); // 1.21：强制中断位
        m_isInitialized = false; 
    }

    // 2. 将后续耗时的存盘与释放逻辑转移至后台线程
    (void)QtConcurrent::run([this]() {

        // B. 物理释放内存 (Swap 技巧)
        {
            QWriteLocker lock(&m_dataLock);
            clearInternal();
            m_is_clearing.store(false);
        }
    });
}

void MftReader::updateActiveDrives(const QStringList& activeDrives) {
    // 2026-05-14 核心修正：使用原子掩码替代 QWriteLocker，消除 UI 线程同步死锁风险
    uint32_t mask = 0;
    QReadLocker lock(&m_dataLock);
    for (const QString& d : activeDrives) {
        std::wstring vol = d.toStdWString();
        if (vol.size() > 1 && (vol.back() == L'\\' || vol.back() == L'/')) vol.pop_back();
        for (size_t i = 0; i < m_drive_list.size(); ++i) {
            if (_wcsicmp(m_drive_list[i].c_str(), vol.c_str()) == 0) {
                mask |= (1 << i);
                break;
            }
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
                if (_wcsicmp(indexedVol.c_str(), vol.c_str()) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                toScan.push_back(vol);
            }
        }
    }

    if (toScan.empty()) {
        // 如果没有新盘需要扫描，且已经初始化，则不需要重建索引
        QReadLocker lock(&m_dataLock);
        if (m_isInitialized) return;
    }

    struct ScannedDrive {
        std::wstring volume;
        MftReader::DriveResult res; 
        bool success = false;
    };
    std::vector<ScannedDrive> scannedResults(toScan.size());
    std::vector<int> scanIndices((int)toScan.size());
    std::iota(scanIndices.begin(), scanIndices.end(), 0);
    std::for_each((std::execution::par), scanIndices.begin(), scanIndices.end(), [&](int i) {
        scannedResults[i].volume = toScan[i];
        scannedResults[i].success = loadMftDirect(toScan[i], scannedResults[i].res);
    });

    QWriteLocker lock(&m_dataLock);
    for (auto& sr : scannedResults) {
        if (!sr.success || sr.res.entries.empty()) continue;
        
        size_t dIdx = m_drive_list.size();
        m_drive_list.push_back(sr.volume);
        if (dIdx < 32) m_drive_active_mask.fetch_or(1 << dIdx);
        mergeDriveResult(sr.volume, sr.res, dIdx);
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;
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

bool MftReader::isMetadataFetched(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_metadata_fetched.size()) return true;
    return m_metadata_fetched[index] == 2;
}

int MftReader::totalCount() const {
    QReadLocker lock(&m_dataLock);
    return (int)m_frns.size();
}

int MftReader::getIndexByKey(uint64_t compositeKey) const {
    QReadLocker lock(&m_dataLock);
    auto it = m_frn_to_idx.find(compositeKey);
    return (it != m_frn_to_idx.end()) ? (int)it->second : -1;
}

uint64_t MftReader::getParentFrnByFrn(uint64_t frn, int driveIdx) const {
    QReadLocker lock(&m_dataLock);
    uint64_t compositeKey = (static_cast<uint64_t>(driveIdx) << 48) | (frn & 0x0000FFFFFFFFFFFFull);
    auto it = m_frn_to_idx.find(compositeKey);
    if (it != m_frn_to_idx.end()) {
        return m_parent_frns[it->second] & 0x0000FFFFFFFFFFFFull;
    }
    return 0;
}

bool MftReader::matchEntry(int i, const QString& query, bool useRegex, bool caseSensitive, 
                          const QStringList& extensionList, bool includeHidden, bool includeSystem,
                          bool includeDollar) const {
    QReadLocker lock(&m_dataLock);
    if (i < 0 || i >= (int)m_frns.size() || m_frns[i] == 0) return false;

    // 驱动器过滤
    size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
    if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) return false;

    // 属性过滤
    uint32_t at = m_attributes[i];
    if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) return false;
    if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) return false;

    const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);

    // $ 过滤逻辑：如果不包含 $，且文件名以 $ 开头，则过滤掉
    if (!includeDollar && p[0] == '$') return false;

    if (query.isEmpty() && extensionList.isEmpty()) return true;

    // 后缀过滤
    if (!extensionList.isEmpty()) {
        bool extMatch = false;
        size_t nameLen = strlen(p);
        for (const QString& ex : extensionList) {
            QByteArray exUtf8 = (ex.startsWith('.') ? ex : "." + ex).toUtf8();
            if (nameLen >= (size_t)exUtf8.size()) {
                if (_stricmp(p + nameLen - exUtf8.size(), exUtf8.constData()) == 0) {
                    extMatch = true;
                    break;
                }
            }
        }
        if (!extMatch) return false;
    }

    if (query.isEmpty()) return true;

    // 内容过滤
    if (useRegex) {
        QRegularExpression re(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        return re.match(QString::fromUtf8(p)).hasMatch();
    } else {
        QByteArray queryUtf8 = query.toUtf8();
        if (caseSensitive) {
            return (strstr(p, queryUtf8.constData()) != nullptr);
        } else {
            return (StrStrIA(p, queryUtf8.constData()) != nullptr);
        }
    }
}

uint64_t MftReader::getKeyByIndex(int index) const {
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_frns.size()) return 0;
    size_t dIdx = static_cast<size_t>(m_parent_frns[index] >> 48);
    return makeKey(dIdx, m_frns[index]);
}

QString MftReader::getFullPath(int index) const {
    // 2026-05-29 物理修复：重构锁顺序，先持有 m_dataLock，内部调用无锁版本的 getPathFastInternal
    QReadLocker lock(&m_dataLock);
    if (index < 0 || index >= (int)m_frns.size()) return QString();
    uint64_t frn = m_frns[index];
    size_t dIdx = static_cast<size_t>(m_parent_frns[index] >> 48);
    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFastInternal(dIdx, frn));
}

std::wstring MftReader::getPathFast(size_t driveIdx, uint64_t frn) {
    // 2026-05-29 物理修复：公开接口持有读锁，内部调用私有无锁逻辑，解决递归死锁。
    QReadLocker readLock(&m_dataLock);
    return getPathFastInternal(driveIdx, frn);
}

std::wstring MftReader::getPathFastInternal(size_t driveIdx, uint64_t frn) {
    // 2026-05-29 物理修复：内部无锁实现。调用方必须已持有 m_dataLock。
    uint64_t compositeKey = (static_cast<uint64_t>(driveIdx) << 48) | (frn & 0x0000FFFFFFFFFFFFull);

    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        auto it = m_path_cache.find(compositeKey);
        if (it != m_path_cache.end()) return it->second;
    }

    if (!m_isInitialized) return L"";

    std::vector<std::wstring> segments;
    uint64_t cur = frn;
    std::unordered_set<uint64_t> vis;
    while (true) {
        uint64_t curKey = (static_cast<uint64_t>(driveIdx) << 48) | (cur & 0x0000FFFFFFFFFFFFull);
        auto idxIt = m_frn_to_idx.find(curKey);
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

    std::wstring volume = (driveIdx < m_drive_list.size()) ? m_drive_list[driveIdx] : L"C:";
    std::wstring path = volume;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) path += L"\\" + *it;

    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        if (m_path_cache.size() > 200000) { 
            auto it_clear = m_path_cache.begin();
            for (int i = 0; i < 2000; ++i) it_clear = m_path_cache.erase(it_clear);
        }
        m_path_cache[compositeKey] = path;
    }
    return path;
}

std::vector<uint64_t> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, 
                                       const QStringList& extensionList, bool includeHidden, bool includeSystem,
                                       bool includeDollar) {
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
    std::vector<uint64_t> finalRes;
    finalRes.reserve(m_frns.size() / 16);

    size_t total = m_frns.size();
    const size_t grainSize = 4096;
    size_t numChunks = (total + grainSize - 1) / grainSize;
    std::vector<size_t> chunkIndices(numChunks);
    std::iota(chunkIndices.begin(), chunkIndices.end(), 0);

    // 2026-05-29 极致算法重构：返回稳定的复合 FRN 主键
    if (hasQuery && !useRegex && !caseSensitive && !hasExt) {
        auto it_start = std::lower_bound(m_sorted_indices.begin(), m_sorted_indices.end(), queryUtf8.constData(), 
            [this](uint32_t idx, const char* q) {
                const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
                return _strnicmp(name, q, strlen(q)) < 0;
            });
        
        for (auto it = it_start; it != m_sorted_indices.end(); ++it) {
            uint32_t i = *it;
            if (m_frns[i] == 0) continue;
            
            const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
            if (_strnicmp(p, queryUtf8.constData(), queryUtf8.size()) != 0) break; 

            // $ 过滤
            if (!includeDollar && p[0] == '$') continue;

            size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
            if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;
            
            uint32_t at = m_attributes[i];
            if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;

            finalRes.push_back(makeKey(dIdx, m_frns[i]));
            if (finalRes.size() > 200000) break; 
        }

        for (size_t i = m_sorted_indices.size(); i < m_frns.size(); ++i) {
            if (m_frns[i] == 0) continue;
            const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
            if (_strnicmp(p, queryUtf8.constData(), queryUtf8.size()) == 0) {
                // $ 过滤
                if (!includeDollar && p[0] == '$') continue;

                size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
                if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;
                uint32_t at = m_attributes[i];
                if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
                if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;
                finalRes.push_back(makeKey(dIdx, m_frns[i]));
            }
        }
    } else {
        std::for_each((std::execution::par), chunkIndices.begin(), chunkIndices.end(), [&](size_t chunkIdx) {
            std::vector<uint64_t> localRes;
            localRes.reserve(grainSize / 16);
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
                
                // $ 过滤
                if (!includeDollar && p[0] == '$') continue;

                if (!hasQuery && !hasExt) {
                    localRes.push_back(makeKey(dIdx, m_frns[i]));
                    continue;
                }

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
                    localRes.push_back(makeKey(dIdx, m_frns[i]));
                } else {
                    bool match = false;
                    if (useRegex) {
                        match = re.match(QString::fromUtf8(p)).hasMatch();
                    } else {
                        if (caseSensitive) {
                            match = (strstr(p, queryUtf8.constData()) != nullptr);
                        } else {
                            match = (StrStrIA(p, queryUtf8.constData()) != nullptr);
                        }
                    }
                    if (match) localRes.push_back(makeKey(dIdx, m_frns[i]));
                }
            }
            if (!localRes.empty()) { std::lock_guard<std::mutex> l(mtx); finalRes.insert(finalRes.end(), localRes.begin(), localRes.end()); }
        });
    }
    return finalRes;
}


void MftReader::compact() {
    // 2026-06-xx 状态标记：碎片整理期间阻止其他写操作
    m_is_compacting.store(true);
    // 2026-05-14 内存管理优化：执行碎片整理，回收无效条目和字符串池空间
    std::vector<uint64_t>  new_frns;
    std::vector<uint64_t>  new_parent_frns;
    std::vector<int64_t>   new_sizes;
    std::vector<int64_t>   new_timestamps;
    std::vector<uint32_t>  new_name_offsets;
    std::vector<uint32_t>  new_attributes;
    std::vector<uint8_t>   new_metadata_fetched;
    std::vector<uint8_t>   new_string_pool;

    size_t count = m_frns.size();
    new_frns.reserve(count - m_dead_count);
    new_parent_frns.reserve(count - m_dead_count);
    new_sizes.reserve(count - m_dead_count);
    new_timestamps.reserve(count - m_dead_count);
    new_name_offsets.reserve(count - m_dead_count);
    new_attributes.reserve(count - m_dead_count);
    new_metadata_fetched.reserve(count - m_dead_count);
    new_string_pool.reserve(m_string_pool.size() - m_wasted_string_bytes);

    m_frn_to_idx.clear();
    for (int i = 0; i < 32; ++i) m_drive_entry_indices[i].clear();

    for (size_t i = 0; i < count; ++i) {
        if (m_frns[i] == 0) continue;
        
        uint32_t newIdx = (uint32_t)new_frns.size();
        size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
        uint64_t key = makeKey(dIdx, m_frns[i]);
        m_frn_to_idx[key] = newIdx;
        if (dIdx < 32) m_drive_entry_indices[dIdx].push_back(newIdx);
        
        new_frns.push_back(m_frns[i]);
        new_parent_frns.push_back(m_parent_frns[i]);
        new_sizes.push_back(m_sizes[i]);
        new_timestamps.push_back(m_timestamps[i]);
        new_attributes.push_back(m_attributes[i]);
        new_metadata_fetched.push_back(m_metadata_fetched[i]);
        
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
    m_metadata_fetched = std::move(new_metadata_fetched);
    m_string_pool = std::move(new_string_pool);

    m_dead_count = 0;
    m_wasted_string_bytes = 0;
    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_is_compacting.store(false);
}

bool MftReader::loadMftDirect(const std::wstring& volume, MftReader::DriveResult& result) {
    // 极致工业级优化方案 C：SoA 结构内存预分配优化
    // 预估 NTFS 卷的文件数量，提前 reserve 以减少动态扩容带来的内存碎片与拷贝开销
    std::wstring dev = L"\\\\.\\" + volume;
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 2026-05-14 获取根目录句柄作为 Hint，这对于 OpenFileById 的稳定性至关重要
    std::wstring rootPath = volume + L"\\";
    // 修正：赋予 FILE_READ_ATTRIBUTES 权限
    HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    USN_JOURNAL_DATA_V0 j; DWORD cb;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &j, sizeof(j), &cb, NULL)) { 
        if (hHint != INVALID_HANDLE_VALUE) CloseHandle(hHint);
        CloseHandle(h); return false; 
    }

    // 工业级启发式预分配：根据日记账条目数预估文件总数，平均 150 字节一个条目
    size_t estimatedCount = static_cast<size_t>(j.NextUsn / 150);
    if (estimatedCount > 100000) result.entries.reserve(estimatedCount);

    MFT_ENUM_DATA_V0 ed = {0}; ed.HighUsn = j.NextUsn;
    std::vector<uint8_t> buf(1024 * 1024);
    while (DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &ed, sizeof(ed), buf.data(), (DWORD)buf.size(), &cb, NULL)) {
        if (m_abort_scan.load()) break; // 1.21：强制中断
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
                struct V3_LAYOUT {
                    DWORD RecordLength; WORD MajorVersion; WORD MinorVersion;
                    BYTE FileReferenceNumber[16]; BYTE ParentFileReferenceNumber[16];
                    USN Usn; LARGE_INTEGER TimeStamp; DWORD Reason; DWORD SourceInfo;
                    DWORD SecurityId; DWORD FileAttributes; WORD FileNameLength; WORD FileNameOffset;
                } *rec = reinterpret_cast<V3_LAYOUT*>(p);
                frn = *reinterpret_cast<uint64_t*>(rec->FileReferenceNumber);
                parentFrn = *reinterpret_cast<uint64_t*>(rec->ParentFileReferenceNumber);
                timestamp = rec->TimeStamp;
                attr = rec->FileAttributes;
                fileNameLength = rec->FileNameLength;
                fileNameOffset = rec->FileNameOffset;
            } else {
                p += header->RecordLength; continue;
            }

            // 2026-05-14 极致性能优化：全量扫描阶段仅获取核心字段，将重量级 I/O 转移至延迟补全队列
            MftReader::RawEntry e; 
            e.frn = frn; 
            e.parentFrn = parentFrn;
            e.size = 0; 
            e.attributes = attr;
            e.modifyTime = filetimeToUnixMs(timestamp.QuadPart);
            
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
    m_metadata_fetched.reserve(m_metadata_fetched.size() + count);
    
    // 方案三：精准写入优化
    if (driveIdx < 32) {
        m_drive_entry_indices[driveIdx].reserve(m_drive_entry_indices[driveIdx].size() + count);
    }

    for (const auto& e : result.entries) {
        uint32_t newIdx = (uint32_t)m_frns.size();
        m_frns.push_back(e.frn);
        m_parent_frns.push_back((static_cast<uint64_t>(driveIdx) << 48) | (e.parentFrn & 0x0000FFFFFFFFFFFFull));
        m_sizes.push_back(e.size); // 2026-05-14 修正：将扫描到的大小压入 SoA
        m_timestamps.push_back(e.modifyTime); m_attributes.push_back(e.attributes);
        m_metadata_fetched.push_back(0);
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), e.nameUtf8.begin(), e.nameUtf8.end());
        m_string_pool.push_back('\0');

        if (driveIdx < 32) {
            m_drive_entry_indices[driveIdx].push_back(newIdx);
        }
    }
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear();
    for (int i = 0; i < 32; ++i) m_drive_entry_indices[i].clear();

    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
            uint64_t key = makeKey(dIdx, m_frns[i]);
            m_frn_to_idx[key] = (uint32_t)i;
            if (dIdx < 32) m_drive_entry_indices[dIdx].push_back((uint32_t)i);
        }
    }
}

void MftReader::buildSortedIndices() {
    // 2026-05-14 性能增强：构建预排序索引，支持二分查找 O(log N)
    m_sorted_indices.resize(m_frns.size());
    std::iota(m_sorted_indices.begin(), m_sorted_indices.end(), 0);
    std::sort((std::execution::par), m_sorted_indices.begin(), m_sorted_indices.end(), [this](uint32_t a, uint32_t b) {
        const char* s1 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[a]);
        const char* s2 = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[b]);
        return _stricmp(s1, s2) < 0;
    });
}

void MftReader::requestMetadata(int index) {
    // 2026-06-xx 工业级 UI 响应优化：
    // 在获取重型写锁前，先使用读锁进行状态预检。
    // 这可以防止在列表快速滚动时，成千上万个 requestMetadata 任务因排队等待写锁而导致主线程假死。
    {
        QReadLocker readLock(&m_dataLock);
        if (index < 0 || index >= (int)m_frns.size() || m_frns[index] == 0) return;
        if (m_metadata_fetched[index] != 0) return; // 状态机检查 (读模式)
    }

    // 2026-05-14 工业级异步补全架构：仅在 UI 可见区域按需拉取物理属性
    QWriteLocker writeLock(&m_dataLock);
    if (index < 0 || index >= (int)m_frns.size() || m_frns[index] == 0) return;
    
    // 二次检查 (写模式，确保原子性)
    if (m_metadata_fetched[index] != 0) return; 
    m_metadata_fetched[index] = 1; // 标记为拉取中，防止重复触发并发任务

    uint64_t frn = m_frns[index];
    size_t dIdx = static_cast<size_t>(m_parent_frns[index] >> 48);
    if (dIdx >= m_drive_list.size()) {
        m_metadata_fetched[index] = 0;
        return;
    }
    std::wstring volume = m_drive_list[dIdx];
    writeLock.unlock();

    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        m_metadata_queue.push_back({index, frn, volume});
    }
    processMetadataQueue();
}

void MftReader::processMetadataQueue() {
    // 2026-06-xx 工业级节流：限制并发拉取元数据的线程数为 4
    if (m_active_metadata_tasks.load() >= 4) return;

    MetadataTask task;
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        if (m_metadata_queue.empty()) return;
        task = m_metadata_queue.back(); // 优先处理最新的请求 (LIFO，适配 UI 滚动)
        m_metadata_queue.pop_back();
    }

    m_active_metadata_tasks.fetch_add(1);
    (void)QtConcurrent::run([this, task]() {
        int index = task.index;
        uint64_t frn = task.frn;
        std::wstring volume = task.volume;

        // 2026-05-14 极致性能重构：对标 Rust 原版，采用 API 分级拉取策略
        // 1. 优先使用 GetFileAttributesExW (不涉及文件句柄，非侵入式，性能极高)
        QString fullPath = getFullPath(index);
        WIN32_FILE_ATTRIBUTE_DATA attrData;
        if (GetFileAttributesExW(reinterpret_cast<const wchar_t*>(fullPath.utf16()), GetFileExInfoStandard, &attrData)) {
            QWriteLocker lock(&m_dataLock);
            if (index < (int)m_frns.size() && m_frns[index] == frn) {
                m_sizes[index] = (static_cast<uint64_t>(attrData.nFileSizeHigh) << 32) | attrData.nFileSizeLow;
                m_timestamps[index] = filetimeToUnixMs((static_cast<int64_t>(attrData.ftLastWriteTime.dwHighDateTime) << 32) | attrData.ftLastWriteTime.dwLowDateTime);
                m_attributes[index] = attrData.dwFileAttributes;
                m_metadata_fetched[index] = 2;
                lock.unlock();
                emit dataChanged(index);
                return;
            }
        }

        // 2. 退化方案：对于特殊文件（如被独占锁定但允许属性读取的文件），使用 OpenFileById
        std::wstring rootPath = volume + L"\\";
        HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hHint != INVALID_HANDLE_VALUE) {
            FILE_ID_DESCRIPTOR id = { sizeof(FILE_ID_DESCRIPTOR), FileIdType };
            id.FileId.QuadPart = frn;
            HANDLE hFile = OpenFileById(hHint, &id, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
            if (hFile != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION bhfi;
                if (GetFileInformationByHandle(hFile, &bhfi)) {
                    QWriteLocker writeLock(&m_dataLock);
                    if (index < (int)m_frns.size() && m_frns[index] == frn) {
                        m_sizes[index] = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                        m_timestamps[index] = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                        m_attributes[index] = bhfi.dwFileAttributes;
                        m_metadata_fetched[index] = 2;
                    }
                }
                CloseHandle(hFile);
            }
            CloseHandle(hHint);
        }

        QWriteLocker lock(&m_dataLock);
        if (index < (int)m_metadata_fetched.size() && m_metadata_fetched[index] == 1) {
            if (m_metadata_fetched[index] != 2) m_metadata_fetched[index] = 0; 
        }
        lock.unlock();
        emit dataChanged(index); 

        m_active_metadata_tasks.fetch_sub(1);
        processMetadataQueue(); // 递归触发下一个任务
    });
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
