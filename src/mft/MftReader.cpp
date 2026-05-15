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
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent>
#include <QFuture>
#include <QFileIconProvider>
#include <QFileInfo>

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

MftReader::MftReader() : m_path_cache(100000) {
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
    m_metadata_fetched.clear();
    m_string_pool.clear();

    m_mmap_datas.clear();
    m_drive_views.clear();

    {
        QWriteLocker lock(&m_overlayLock);
        m_overlay.clear();
        m_overlay_frns.clear();
    }
    m_drive_list.clear();
    m_drive_active_mask = 0;
    m_frn_to_idx.clear();
    m_sorted_indices.clear();
    m_path_cache.clear();
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
    std::for_each((std::execution::par), scanIndices.begin(), scanIndices.end(), [&](int i) {
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
            auto mmapData = std::make_unique<ScchMmapData>();
            std::unordered_map<std::string, uint64_t> usnMap;

            if (ScchCache::loadMmap(entry.path().string().c_str(), *mmapData, usnMap) == ScchResult::Ok) {
                ScchMmapData* pData = mmapData.get();
                m_mmap_datas.push_back(std::move(mmapData));
                size_t dIdx = m_drive_list.size();

                DriveView view;
                view.frns = pData->frns;
                view.parent_frns = pData->parent_frns;
                view.sizes = pData->sizes;
                view.timestamps = pData->timestamps;
                view.name_offsets = pData->name_offsets;
                view.attributes = pData->attributes;
                view.metadata_fetched = pData->metadata_fetched;
                view.string_pool = pData->string_pool;
                view.sorted_indices = pData->sorted_indices;
                view.count = pData->record_count;
                view.pool_size = pData->pool_size;
                view.drive_idx = dIdx;

                // 计算全局偏移：之前所有 view 的 count 之和
                view.global_offset = 0;
                for(const auto& v : m_drive_views) view.global_offset += (uint32_t)v.count;

                m_drive_views.push_back(view);

                for (const auto& [drive, usn] : usnMap) {
                    std::wstring wDrive = QString::fromStdString(drive).toStdWString();
                    m_drive_list.push_back(wDrive);
                    m_next_usns[wDrive] = usn;
                }
            }
        }
    }

    if (m_drive_views.empty()) return false;
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
    std::vector<uint8_t> sp, mf;
    std::vector<uint32_t> local_sorted;
    std::unordered_map<uint32_t, uint32_t> offsetMap;
    std::unordered_map<uint32_t, uint32_t> global_to_local;

    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0 && (m_parent_frns[i] >> 48) == driveIdx) {
            uint32_t localIdx = (uint32_t)f.size();
            global_to_local[(uint32_t)i] = localIdx;

            f.push_back(m_frns[i]);
            pf.push_back(m_parent_frns[i] & 0x0000FFFFFFFFFFFFull);
            s.push_back(m_sizes[i]);
            t.push_back(m_timestamps[i]);
            attr.push_back(m_attributes[i]);
            mf.push_back(m_metadata_fetched[i]);
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

    for (uint32_t gIdx : m_sorted_indices) {
        auto it = global_to_local.find(gIdx);
        if (it != global_to_local.end()) {
            local_sorted.push_back(it->second);
        }
    }

    std::unordered_map<std::string, uint64_t> usnMap;
    usnMap[QString::fromStdWString(volume).toStdString()] = m_next_usns[volume];
    QString path = QString("ArcMeta/cache/%1.scch").arg(QString::fromStdWString(volume).left(1));
    return ScchCache::save(path.toStdString().c_str(), f, pf, s, t, no, attr, mf, sp, local_sorted, usnMap);
}

QString MftReader::getName(int index) const {
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx < (int)m_overlay_frns.size()) {
            auto it = m_overlay.find(m_overlay_frns[overlayIdx]);
            if (it != m_overlay.end()) return QString::fromUtf8(it->second.nameUtf8.c_str());
        }
        return QString();
    }
    QReadLocker lock(&m_dataLock);
    // 检查是否落在 DriveView 中
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) {
            return QString::fromUtf8(reinterpret_cast<const char*>(view.string_pool + view.name_offsets[off]));
        }
        off -= (uint32_t)view.count;
    }
    // 落在 SoA 主数组中
    if (off < (uint32_t)m_name_offsets.size()) {
        return QString::fromUtf8(reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[off]));
    }
    return QString();
}

int64_t MftReader::getSize(int index) const {
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx < (int)m_overlay_frns.size()) {
            auto it = m_overlay.find(m_overlay_frns[overlayIdx]);
            if (it != m_overlay.end()) return it->second.size;
        }
        return 0;
    }
    QReadLocker lock(&m_dataLock);
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) return view.sizes[off];
        off -= (uint32_t)view.count;
    }
    if (off < (uint32_t)m_sizes.size()) return m_sizes[off];
    return 0;
}

int64_t MftReader::getModifyTime(int index) const {
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx < (int)m_overlay_frns.size()) {
            auto it = m_overlay.find(m_overlay_frns[overlayIdx]);
            if (it != m_overlay.end()) return it->second.modifyTime;
        }
        return 0;
    }
    QReadLocker lock(&m_dataLock);
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) return view.timestamps[off];
        off -= (uint32_t)view.count;
    }
    if (off < (uint32_t)m_timestamps.size()) return m_timestamps[off];
    return 0;
}

uint32_t MftReader::getAttributes(int index) const {
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx < (int)m_overlay_frns.size()) {
            auto it = m_overlay.find(m_overlay_frns[overlayIdx]);
            if (it != m_overlay.end()) return it->second.attributes;
        }
        return 0;
    }
    QReadLocker lock(&m_dataLock);
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) return view.attributes[off];
        off -= (uint32_t)view.count;
    }
    if (off < (uint32_t)m_attributes.size()) return m_attributes[off];
    return 0;
}

uint64_t MftReader::getFrn(int index) const {
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx < (int)m_overlay_frns.size()) return m_overlay_frns[overlayIdx];
        return 0;
    }
    QReadLocker lock(&m_dataLock);
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) return view.frns[off];
        off -= (uint32_t)view.count;
    }
    if (off < (uint32_t)m_frns.size()) return m_frns[off];
    return 0;
}

bool MftReader::isDirectory(int index) const {
    return (getAttributes(index) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool MftReader::isMetadataFetched(int index) const {
    if (index < 0) return true;
    QReadLocker lock(&m_dataLock);
    uint32_t off = (uint32_t)index;
    for (const auto& view : m_drive_views) {
        if (off < view.count) return view.metadata_fetched[off] == 2;
        off -= (uint32_t)view.count;
    }
    if (off < (uint32_t)m_metadata_fetched.size()) return m_metadata_fetched[off] == 2;
    return true;
}

int MftReader::totalCount() const {
    QReadLocker lock(&m_dataLock);
    QReadLocker lockO(&m_overlayLock);
    size_t total = m_frns.size();
    for (const auto& view : m_drive_views) total += view.count;
    // Overlay 中包含了新增，同时也可能包含对主索引条目的修改/删除标记
    // 此处返回一个概估值，以维持 UI 响应性能
    return (int)(total + m_overlay_frns.size());
}

QString MftReader::getFullPath(int index) const {
    uint64_t frn = 0;
    size_t dIdx = 0;
    if (index < 0) {
        QReadLocker lock(&m_overlayLock);
        int overlayIdx = -index - 1;
        if (overlayIdx >= (int)m_overlay_frns.size()) return QString();
        frn = m_overlay_frns[overlayIdx];
        auto it = m_overlay.find(frn);
        if (it == m_overlay.end()) return QString();
        dIdx = static_cast<size_t>(it->second.parentFrn >> 48);
    } else {
        QReadLocker lock(&m_dataLock);
        uint32_t off = (uint32_t)index;
        bool found = false;
        for (const auto& view : m_drive_views) {
            if (off < view.count) {
                frn = view.frns[off];
                dIdx = static_cast<size_t>(view.parent_frns[off] >> 48);
                found = true; break;
            }
            off -= (uint32_t)view.count;
        }
        if (!found) {
            if (off < (uint32_t)m_frns.size()) {
                frn = m_frns[off];
                dIdx = static_cast<size_t>(m_parent_frns[off] >> 48);
            } else return QString();
        }
    }
    QReadLocker lock(&m_dataLock);
    std::wstring vol = (dIdx < m_drive_list.size()) ? m_drive_list[dIdx] : L"C:";
    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFast(vol, frn));
}

std::wstring MftReader::getPathFast(const std::wstring& volume, uint64_t frn) {
    {
        auto cached = m_path_cache.get(frn);
        if (cached) return *cached;
    }
    std::vector<std::wstring> segments;
    uint64_t cur = frn;
    std::unordered_set<uint64_t> vis;
    while (true) {
        if (vis.count(cur)) break;
        vis.insert(cur);

        uint64_t parent = 0;
        std::string name;

        // 优先检查 Overlay
        {
            QReadLocker lockO(&m_overlayLock);
            auto itO = m_overlay.find(cur);
            if (itO != m_overlay.end()) {
                if (itO->second.deleted) break;
                name = itO->second.nameUtf8;
                parent = itO->second.parentFrn & 0x0000FFFFFFFFFFFFull;
            }
        }

        // 如果不在 Overlay 中，检查主索引
        if (name.empty()) {
            QReadLocker lockD(&m_dataLock);
            auto idxIt = m_frn_to_idx.find(cur);
            if (idxIt != m_frn_to_idx.end()) {
                uint32_t idx = idxIt->second;
                // 统一数据重定向访问
                uint32_t off = idx;
                bool foundView = false;
                for (const auto& view : m_drive_views) {
                    if (off < view.count) {
                        name = reinterpret_cast<const char*>(view.string_pool + view.name_offsets[off]);
                        parent = view.parent_frns[off] & 0x0000FFFFFFFFFFFFull;
                        foundView = true; break;
                    }
                    off -= (uint32_t)view.count;
                }
                if (!foundView) {
                    if (off < (uint32_t)m_frns.size()) {
                        if (m_frns[off] == 0) break;
                        name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[off]);
                        parent = m_parent_frns[off] & 0x0000FFFFFFFFFFFFull;
                    }
                }
            }
        }

        if (name.empty()) break;
        segments.push_back(QString::fromUtf8(name.c_str()).toStdWString());
        if (parent == 5 || parent == cur || parent == 0) break;
        cur = parent;
    }
    if (segments.empty()) return L"";
    std::wstring path = volume;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) path += L"\\" + *it;
    m_path_cache.put(frn, path);
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

    // 2026-05-14 极致算法优化：如果是简单前缀匹配且非正则、非后缀过滤，直接使用二分查找 O(log N)
    if (hasQuery && !useRegex && !caseSensitive && !hasExt) {
        uint32_t global_base = 0;
        for (const auto& view : m_drive_views) {
            size_t dIdx = view.drive_idx;
            if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) {
                global_base += (uint32_t)view.count;
                continue;
            }

            auto it_start = std::lower_bound(view.sorted_indices, view.sorted_indices + view.count, queryUtf8.constData(),
                [&view](uint32_t idx, const char* q) {
                    const char* name = reinterpret_cast<const char*>(view.string_pool + view.name_offsets[idx]);
                    return _strnicmp(name, q, strlen(q)) < 0;
                });

            for (auto it = it_start; it != view.sorted_indices + view.count; ++it) {
                uint32_t localIdx = *it;
                uint64_t frn = view.frns[localIdx];
                if (frn == 0 || m_overlay.count(frn)) continue;

                const char* p = reinterpret_cast<const char*>(view.string_pool + view.name_offsets[localIdx]);
                if (_strnicmp(p, queryUtf8.constData(), queryUtf8.size()) != 0) break;

                uint32_t at = view.attributes[localIdx];
                if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
                if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;

                finalRes.push_back((int)(global_base + localIdx));
                if (finalRes.size() > 50000) break;
            }
            global_base += (uint32_t)view.count;
            if (finalRes.size() > 50000) break;
        }

        // 处理 SoA 主数组 (std::vector) 中的预排序数据
        if (finalRes.size() <= 50000 && !m_sorted_indices.empty()) {
            auto it_start = std::lower_bound(m_sorted_indices.begin(), m_sorted_indices.end(), queryUtf8.constData(),
                [this](uint32_t idx, const char* q) {
                    const char* name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
                    return _strnicmp(name, q, strlen(q)) < 0;
                });

            for (auto it = it_start; it != m_sorted_indices.end(); ++it) {
                uint32_t i = *it;
                if (m_frns[i] == 0 || m_overlay.count(m_frns[i])) continue;

                const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
                if (_strnicmp(p, queryUtf8.constData(), queryUtf8.size()) != 0) break;

                size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
                if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;
                uint32_t at = m_attributes[i];
                if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
                if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;

                finalRes.push_back((int)(global_base + i));
                if (finalRes.size() > 50000) break;
            }
        }
    } else {
        // 回退到并行线性扫描 (用于复杂子串匹配、正则或后缀过滤)
        std::mutex resMtx;
        uint32_t current_base = 0;

        // 1. 并行扫描 DriveViews
        for (const auto& view : m_drive_views) {
            size_t dIdx = view.drive_idx;
            if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) {
                current_base += (uint32_t)view.count; continue;
            }

            size_t count = view.count;
            const size_t grain = 8192;
            size_t numChunks = (count + grain - 1) / grain;
            std::vector<size_t> chunks(numChunks);
            std::iota(chunks.begin(), chunks.end(), 0);

            std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](size_t chunkIdx) {
                std::vector<int> localRes;
                size_t start = chunkIdx * grain;
                size_t end = (std::min)(start + grain, count);
                for (size_t i = start; i < end; ++i) {
                    uint64_t frn = view.frns[i];
                    if (frn == 0 || m_overlay.count(frn)) continue;
                    uint32_t at = view.attributes[i];
                    if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
                    if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;
                    const char* p = reinterpret_cast<const char*>(view.string_pool + view.name_offsets[i]);
                    if (hasExt) {
                        bool extMatch = false; size_t nameLen = strlen(p);
                        for (const auto& ex : extUtf8) {
                            if (nameLen >= (size_t)ex.size() && _stricmp(p + nameLen - ex.size(), ex.constData()) == 0) { extMatch = true; break; }
                        }
                        if (!extMatch) continue;
                    }
                    bool match = false;
                    if (!hasQuery) match = true;
                    else if (useRegex) match = re.match(QString::fromUtf8(p)).hasMatch();
                    else if (caseSensitive) match = (strstr(p, queryUtf8.constData()) != nullptr);
                    else match = (StrStrIA(p, queryUtf8.constData()) != nullptr);
                    if (match) localRes.push_back((int)(current_base + i));
                }
                if (!localRes.empty()) { std::lock_guard<std::mutex> lock(resMtx); finalRes.insert(finalRes.end(), localRes.begin(), localRes.end()); }
            });
            current_base += (uint32_t)view.count;
        }

        // 2. 并行扫描 SoA vector
        size_t count = m_frns.size();
        if (count > 0) {
            const size_t grain = 8192;
            size_t numChunks = (count + grain - 1) / grain;
            std::vector<size_t> chunks(numChunks);
            std::iota(chunks.begin(), chunks.end(), 0);

            std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](size_t chunkIdx) {
                std::vector<int> localRes;
                size_t start = chunkIdx * grain;
                size_t end = (std::min)(start + grain, count);
                for (size_t i = start; i < end; ++i) {
                    if (m_frns[i] == 0 || m_overlay.count(m_frns[i])) continue;
                    size_t dIdx = static_cast<size_t>(m_parent_frns[i] >> 48);
                    if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;
                    uint32_t at = m_attributes[i];
                    if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
                    if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;
                    const char* p = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
                    if (hasExt) {
                        bool extMatch = false; size_t nameLen = strlen(p);
                        for (const auto& ex : extUtf8) {
                            if (nameLen >= (size_t)ex.size() && _stricmp(p + nameLen - ex.size(), ex.constData()) == 0) { extMatch = true; break; }
                        }
                        if (!extMatch) continue;
                    }
                    bool match = false;
                    if (!hasQuery) match = true;
                    else if (useRegex) match = re.match(QString::fromUtf8(p)).hasMatch();
                    else if (caseSensitive) match = (strstr(p, queryUtf8.constData()) != nullptr);
                    else match = (StrStrIA(p, queryUtf8.constData()) != nullptr);
                    if (match) localRes.push_back((int)(current_base + i));
                }
                if (!localRes.empty()) { std::lock_guard<std::mutex> lock(resMtx); finalRes.insert(finalRes.end(), localRes.begin(), localRes.end()); }
            });
        }

    // 并行扫描 Overlay (如果数据量够大)
    for (size_t i = 0; i < m_overlay_frns.size(); ++i) {
        uint64_t frn = m_overlay_frns[i];
        auto it = m_overlay.find(frn);
        if (it == m_overlay.end() || it->second.deleted) continue;

        size_t dIdx = static_cast<size_t>(it->second.parentFrn >> 48);
        if (dIdx >= 32 || !(m_drive_active_mask.load(std::memory_order_relaxed) & (1 << dIdx))) continue;

        uint32_t at = it->second.attributes;
        if (!includeHidden && (at & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (!includeSystem && (at & FILE_ATTRIBUTE_SYSTEM)) continue;

        const char* p = it->second.nameUtf8.c_str();
        if (hasExt) {
            bool extMatch = false;
            size_t nameLen = strlen(p);
            for (const auto& ex : extUtf8) {
                if (nameLen >= (size_t)ex.size()) {
                    if (_stricmp(p + nameLen - ex.size(), ex.constData()) == 0) { extMatch = true; break; }
                }
            }
            if (!extMatch) continue;
        }

        if (!hasQuery) {
            finalRes.push_back(-(int)i - 1);
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
            if (match) finalRes.push_back(-(int)i - 1);
        }
    }
    }
    return QVector<int>(finalRes.begin(), finalRes.end());
}

void MftReader::updateEntryFromUsn(USN_RECORD_V2* record, const std::wstring& volume) {
    USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(record);
    uint64_t frn, parentFrn;
    uint32_t attr;
    LARGE_INTEGER timestamp;
    WORD fileNameLength, fileNameOffset;

    // 2026-05-14 核心排查：针对 V2 (64bit FRN) 和 V3 (128bit FRN) 进行严格的偏移匹配
    if (header->MajorVersion == 2) {
        frn = record->FileReferenceNumber;
        parentFrn = record->ParentFileReferenceNumber;
        attr = record->FileAttributes;
        timestamp = record->TimeStamp;
        fileNameLength = record->FileNameLength;
        fileNameOffset = record->FileNameOffset;
    } else if (header->MajorVersion == 3) {
        // 手动映射 V3 布局，避免 SDK 定义缺失导致的读取错误
        struct V3_LAYOUT {
            DWORD RecordLength; WORD MajorVersion; WORD MinorVersion;
            BYTE FileReferenceNumber[16]; BYTE ParentFileReferenceNumber[16];
            USN Usn; LARGE_INTEGER TimeStamp; DWORD Reason; DWORD SourceInfo;
            DWORD SecurityId; DWORD FileAttributes; WORD FileNameLength; WORD FileNameOffset;
        } *v3 = reinterpret_cast<V3_LAYOUT*>(record);
        frn = *reinterpret_cast<uint64_t*>(v3->FileReferenceNumber);
        parentFrn = *reinterpret_cast<uint64_t*>(v3->ParentFileReferenceNumber);
        attr = v3->FileAttributes;
        timestamp = v3->TimeStamp;
        fileNameLength = v3->FileNameLength;
        fileNameOffset = v3->FileNameOffset;
    } else return;

    uint64_t fileSize = 0;
    int64_t finalModifyTime = filetimeToUnixMs(timestamp.QuadPart);
    uint32_t finalAttr = attr;
    bool fetchedSuccess = false;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring rootPath = volume + L"\\";
        // 2026-05-14 修正：hHint 需要 FILE_READ_ATTRIBUTES 权限来辅助 OpenFileById
        HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hHint != INVALID_HANDLE_VALUE) {
            FILE_ID_DESCRIPTOR id = {0};
            id.dwSize = sizeof(FILE_ID_DESCRIPTOR);
            id.Type = FileIdType;
            id.FileId.QuadPart = frn;
            // 2026-05-14 核心修正：OpenFileById 的 DesiredAccess 不能为 0，必须至少为 FILE_READ_ATTRIBUTES 才能获取文件大小
            HANDLE hFile = OpenFileById(hHint, &id, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
            if (hFile != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION bhfi;
                if (GetFileInformationByHandle(hFile, &bhfi)) {
                    fileSize = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                    finalAttr = bhfi.dwFileAttributes;
                    finalModifyTime = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                    fetchedSuccess = true;
                }
                CloseHandle(hFile);
            }
            CloseHandle(hHint);
        }
    }

    size_t dIdx = 0;
    {
        QReadLocker lock(&m_dataLock);
        for (size_t i = 0; i < m_drive_list.size(); ++i) { if (m_drive_list[i] == volume) { dIdx = i; break; } }
    }
    uint64_t encodedPf = (static_cast<uint64_t>(dIdx) << 48) | (parentFrn & 0x0000FFFFFFFFFFFFull);
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(reinterpret_cast<uint8_t*>(record) + fileNameOffset), fileNameLength / 2);

    {
        QWriteLocker lock(&m_overlayLock);
        OverlayEntry& e = m_overlay[frn];
        bool isNew = e.nameUtf8.empty();
        if (isNew) m_overlay_frns.push_back(frn);
        
        e.frn = frn;
        e.parentFrn = encodedPf;
        e.size = fileSize;
        e.attributes = finalAttr;
        e.modifyTime = (finalModifyTime > 0) ? finalModifyTime : filetimeToUnixMs(timestamp.QuadPart);
        e.nameUtf8 = name.toUtf8().toStdString();
        e.deleted = false;

        int overlayIdx = -1;
        if (isNew) {
            overlayIdx = (int)m_overlay_frns.size() - 1;
        } else {
            for(int i=0; i<(int)m_overlay_frns.size(); ++i) {
                if(m_overlay_frns[i] == frn) { overlayIdx = i; break; }
            }
        }

        m_path_cache.remove(frn);
        m_next_usns[volume] = record->Usn;
        emit dataChanged(-overlayIdx - 1);
    }
}

void MftReader::removeEntryByFrn(const std::wstring& volume, uint64_t frn) {
    Q_UNUSED(volume);
    {
        QWriteLocker lock(&m_overlayLock);
        OverlayEntry& e = m_overlay[frn];
        bool isNew = e.nameUtf8.empty();
        if (isNew) m_overlay_frns.push_back(frn);
        
        e.frn = frn;
        e.deleted = true;

        int overlayIdx = -1;
        if (isNew) {
            overlayIdx = (int)m_overlay_frns.size() - 1;
        } else {
            for(int i=0; i<(int)m_overlay_frns.size(); ++i) {
                if(m_overlay_frns[i] == frn) { overlayIdx = i; break; }
            }
        }

        m_path_cache.remove(frn);
        emit dataChanged(-overlayIdx - 1);
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
    for (size_t i = 0; i < count; ++i) {
        if (m_frns[i] == 0) continue;
        
        uint32_t newIdx = (uint32_t)new_frns.size();
        m_frn_to_idx[m_frns[i]] = newIdx;
        
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
}

bool MftReader::loadMftDirect(const std::wstring& volume, MftReader::DriveResult& result) {
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
    for (const auto& e : result.entries) {
        m_frns.push_back(e.frn);
        m_parent_frns.push_back((static_cast<uint64_t>(driveIdx) << 48) | (e.parentFrn & 0x0000FFFFFFFFFFFFull));
        m_sizes.push_back(e.size); // 2026-05-14 修正：将扫描到的大小压入 SoA
        m_timestamps.push_back(e.modifyTime); m_attributes.push_back(e.attributes);
        m_metadata_fetched.push_back(0);
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), e.nameUtf8.begin(), e.nameUtf8.end());
        m_string_pool.push_back('\0');
    }
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear();
    uint32_t global_base = 0;
    for (const auto& view : m_drive_views) {
        for (size_t i = 0; i < view.count; ++i) {
            if (view.frns[i] != 0) m_frn_to_idx[view.frns[i]] = global_base + (uint32_t)i;
        }
        global_base += (uint32_t)view.count;
    }
    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            m_frn_to_idx[m_frns[i]] = global_base + (uint32_t)i;
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
    if (index < 0) return;

    // 2026-05-14 工业级异步补全架构：仅在 UI 可见区域按需拉取物理属性
    QWriteLocker writeLock(&m_dataLock);
    
    uint64_t frn = 0;
    size_t dIdx = 0;
    uint8_t* p_fetched = nullptr;

    uint32_t off = (uint32_t)index;
    bool found = false;
    for (auto& view : m_drive_views) {
        if (off < view.count) {
            frn = view.frns[off];
            dIdx = view.drive_idx;
            p_fetched = const_cast<uint8_t*>(&view.metadata_fetched[off]);
            found = true; break;
        }
        off -= (uint32_t)view.count;
    }

    if (!found) {
        if (off < (uint32_t)m_frns.size()) {
            frn = m_frns[off];
            if (frn == 0) return;
            dIdx = static_cast<size_t>(m_parent_frns[off] >> 48);
            p_fetched = &m_metadata_fetched[off];
        } else return;
    }

    if (*p_fetched != 0) return;
    *p_fetched = 1; // 标记为拉取中

    if (dIdx >= m_drive_list.size()) {
        m_metadata_fetched[index] = 0;
        return;
    }
    std::wstring volume = m_drive_list[dIdx];
    writeLock.unlock();

    QtConcurrent::run([this, index, frn, volume]() {
        std::wstring rootPath = volume + L"\\";
        HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hHint == INVALID_HANDLE_VALUE) {
            QWriteLocker lock(&m_dataLock);
            // 重置状态 (简化逻辑，实际可能需要更精确的 DriveView 寻址)
            return;
        }

        FILE_ID_DESCRIPTOR id = { sizeof(FILE_ID_DESCRIPTOR), FileIdType };
        id.FileId.QuadPart = frn;

        HANDLE hFile = OpenFileById(hHint, &id, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
        if (hFile != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION bhfi;
            if (GetFileInformationByHandle(hFile, &bhfi)) {
                QWriteLocker writeLock(&m_dataLock);

                uint64_t fileSize = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                int64_t modifyTime = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                uint32_t attr = bhfi.dwFileAttributes;

                // 2026-05-14：异步获取到的元数据统一进入 Overlay，避免尝试修改只读 Mmap 内存
                std::string name;
                uint64_t parentFrn = 0;

                // 先尝试从现有视图中获取名称和父级 FRN
                {
                    QReadLocker lockD(&m_dataLock);
                    uint32_t off = (uint32_t)index;
                    bool found = false;
                    for (const auto& view : m_drive_views) {
                        if (off < view.count) {
                            if (view.frns[off] == frn) {
                                name = reinterpret_cast<const char*>(view.string_pool + view.name_offsets[off]);
                                parentFrn = view.parent_frns[off];
                                found = true;
                            }
                            break;
                        }
                        off -= (uint32_t)view.count;
                    }
                    if (!found && off < (uint32_t)m_frns.size() && m_frns[off] == frn) {
                        name = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[off]);
                        parentFrn = m_parent_frns[off];
                        found = true;
                    }
                }

                if (!name.empty()) {
                    QWriteLocker lockO(&m_overlayLock);
                    OverlayEntry& e = m_overlay[frn];
                    if (e.nameUtf8.empty()) m_overlay_frns.push_back(frn);

                    e.frn = frn;
                    e.parentFrn = parentFrn;
                    e.size = fileSize;
                    e.attributes = attr;
                    e.modifyTime = modifyTime;
                    e.nameUtf8 = name;
                    e.deleted = false;
                }
            }
            CloseHandle(hFile);
        }
        CloseHandle(hHint);
        
        // 2026-05-14 性能优化：触发局部模型刷新，避免百万行全量重绘
        emit dataChanged(index); 
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
