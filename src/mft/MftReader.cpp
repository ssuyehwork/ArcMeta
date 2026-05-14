#include "MftReader.h"
#include "UsnWatcher.h"
#include <winioctl.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <algorithm>
#include <execution>
#include <mutex>
#include <numeric>
#include <QDebug>
#include <QRegularExpression>
#include <QDir>
#include <QDateTime>

namespace ArcMeta {

// 时间戳转换函数：Windows FILETIME (100ns) -> Unix 毫秒
static int64_t filetimeToUnixMs(int64_t filetime) {
    if (filetime == 0) return 0;
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
}

void MftReader::clear() {
    // 1. 先在锁外停止所有 watcher，防止死锁
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

    // 2. 加锁执行内部清理
    QWriteLocker lock(&m_dataLock);
    clearInternal();
}

void MftReader::buildIndex(const QStringList& drives) {
    clear(); // 先清理，此时无锁

    QWriteLocker lock(&m_dataLock);
    
    QStringList targetDrives = drives;
    if (targetDrives.isEmpty()) {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (1 << i)) {
                QString root = QString(QChar('A' + i)) + ":\\";
                if (GetDriveTypeW(reinterpret_cast<const wchar_t*>(root.utf16())) == DRIVE_FIXED) {
                    targetDrives << root.left(2);
                }
            }
        }
    }

    for (const QString& driveStr : targetDrives) {
        std::wstring volume = driveStr.toStdWString();
        if (volume.size() > 2 && volume.back() == L'\\') volume.pop_back(); // "C:\" -> "C:"

        DriveResult result;
        if (loadMftDirect(volume, result)) {
            size_t driveIdx = m_drive_list.size();
            m_drive_list.push_back(volume);
            m_next_usns[volume] = result.nextUsn;
            mergeDriveResult(volume, result, driveIdx);
        }
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    // 启动监控
    for (const auto& [volume, usn] : m_next_usns) {
        auto* watcher = new UsnWatcher(volume, usn, this);
        m_watchers.push_back(watcher);
        watcher->start();
    }

    // 扫描完成后自动保存缓存
    lock.unlock();
    saveToCache();
}

bool MftReader::loadFromCache() {
    clearInternal(); // 不加锁的内部版本

    std::unordered_map<std::string, uint64_t> usnMap;
    QWriteLocker lock(&m_dataLock);

    // 调用 ScchCache 接口，使用规范定义的常量
    ScchResult result = ScchCache::load(
        SCCH_DEFAULT_PATH,
        m_frns, m_parent_frns, m_sizes, m_timestamps,
        m_name_offsets, m_attributes, m_string_pool, usnMap
    );

    if (result != ScchResult::Ok) return false;

    for (const auto& [drive, usn] : usnMap) {
        std::wstring wDrive = QString::fromStdString(drive).toStdWString();
        m_next_usns[wDrive] = usn;
        m_drive_list.push_back(wDrive);
    }

    rebuildFrnToIndexMap();
    buildSortedIndices();
    m_isInitialized = true;

    // 为每个盘启动监控并执行离线追平
    for (const auto& [volume, usn] : m_next_usns) {
        auto* watcher = new UsnWatcher(volume, usn, this);
        m_watchers.push_back(watcher);
        watcher->start();
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
    uint64_t parentInfo = m_parent_frns[index];
    size_t driveIdx = static_cast<size_t>(parentInfo >> 48);
    
    std::wstring volume = L"C:";
    if (driveIdx < m_drive_list.size()) {
        volume = m_drive_list[driveIdx];
    }

    // 调用 getPathFast 实现路径重建
    return QString::fromStdWString(const_cast<MftReader*>(this)->getPathFast(volume, frn));
}

std::wstring MftReader::getPathFast(const std::wstring& volume, uint64_t frn) {
    // 1. 先查缓存 (使用独立互斥锁保护)
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        auto it = m_path_cache.find(frn);
        if (it != m_path_cache.end()) return it->second;
    }

    std::vector<std::wstring> segments;
    uint64_t currentFrn = frn;
    std::unordered_set<uint64_t> visited;

    while (true) {
        auto idxIt = m_frn_to_idx.find(currentFrn);
        if (idxIt == m_frn_to_idx.end()) break;
        if (visited.count(currentFrn)) break; // 环路保护

        visited.insert(currentFrn);
        uint32_t idx = idxIt->second;

        // 从字符串池取名
        const char* namePtr = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[idx]);
        segments.push_back(QString::fromUtf8(namePtr).toStdWString());

        // 提取 48 位父 FRN 掩码
        uint64_t parentFrn = m_parent_frns[idx] & 0x0000FFFFFFFFFFFFull;

        // 根节点保护
        if (parentFrn == 5 || parentFrn == currentFrn || parentFrn == 0) break;
        currentFrn = parentFrn;
    }

    if (segments.empty()) return L"";

    std::wstring fullPath = volume; // 注入盘符
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        fullPath += L"\\" + *it;
    }

    // 存入缓存
    {
        std::lock_guard<std::mutex> lock(m_pathCacheMutex);
        m_path_cache[frn] = fullPath;
    }
    return fullPath;
}

QVector<int> MftReader::search(const QString& query, bool useRegex, bool caseSensitive, 
                              const QStringList& extensionList, bool includeHidden, bool includeSystem) {
    QReadLocker lock(&m_dataLock);
    if (!m_isInitialized) return {};

    std::vector<int> results;
    int total = (int)m_frns.size();

    QRegularExpression re;
    if (useRegex && !query.isEmpty()) {
        re = QRegularExpression(query, caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
    }

    std::mutex mtx;
    std::vector<int> indices(total);
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](int i) {
        if (m_frns[i] == 0) return; // 跳过已删除条目

        uint32_t attr = m_attributes[i];
        if (!includeHidden && (attr & FILE_ATTRIBUTE_HIDDEN)) return;
        if (!includeSystem && (attr & FILE_ATTRIBUTE_SYSTEM)) return;

        const char* namePtr = reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[i]);
        QString name = QString::fromUtf8(namePtr);

        // 扩展名过滤
        if (!extensionList.isEmpty()) {
            bool extMatch = false;
            for (const QString& ext : extensionList) {
                if (name.endsWith("." + ext, Qt::CaseInsensitive)) {
                    extMatch = true;
                    break;
                }
            }
            if (!extMatch) return;
        }

        bool match = false;
        if (query.isEmpty()) {
            match = true;
        } else if (useRegex) {
            match = re.match(name).hasMatch();
        } else {
            if (caseSensitive) match = name.contains(query, Qt::CaseSensitive);
            else match = name.contains(query, Qt::CaseInsensitive);
        }

        if (match) {
            std::lock_guard<std::mutex> l(mtx);
            results.push_back(i);
        }
    });

    return QVector<int>(results.begin(), results.end());
}

void MftReader::updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume) {
    QWriteLocker lock(&m_dataLock);
    
    uint64_t frn = record->FileReferenceNumber;
    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(
        reinterpret_cast<uint8_t*>(record) + record->FileNameOffset), record->FileNameLength / 2);
    
    // 查找盘符索引
    uint64_t driveIdx = 0;
    for (size_t i = 0; i < m_drive_list.size(); ++i) {
        if (m_drive_list[i] == volume) {
            driveIdx = i;
            break;
        }
    }

    uint64_t encodedParent = (driveIdx << 48) | (record->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFFull);

    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        // 更新现有条目
        uint32_t idx = it->second;
        m_parent_frns[idx] = encodedParent;
        m_attributes[idx] = record->FileAttributes;
        m_timestamps[idx] = filetimeToUnixMs(record->TimeStamp.QuadPart);
        
        QByteArray utf8 = name.toUtf8();
        m_name_offsets[idx] = (uint32_t)m_string_pool.size();
        m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
        m_string_pool.push_back('\0');
    } else {
        // 新增条目
        uint32_t newIdx = (uint32_t)m_frns.size();
        m_frns.push_back(frn);
        m_parent_frns.push_back(encodedParent);
        m_sizes.push_back(0);
        m_timestamps.push_back(filetimeToUnixMs(record->TimeStamp.QuadPart));
        m_attributes.push_back(record->FileAttributes);
        
        QByteArray utf8 = name.toUtf8();
        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), utf8.begin(), utf8.end());
        m_string_pool.push_back('\0');
        
        m_frn_to_idx[frn] = newIdx;
    }

    // 路径变更，失效缓存
    {
        std::lock_guard<std::mutex> l(m_pathCacheMutex);
        m_path_cache.erase(frn);
    }

    m_next_usns[volume] = record->Usn;
    m_dirty_count++;
    if (m_dirty_count >= 1000) {
        m_dirty_count = 0;
        // 在写锁保护下保存，虽然 saveToCache 内部会尝试加读锁，
        // 但 QReadWriteLock 允许在持有写锁时加读锁（重入性）
        saveToCache();
    }
    emit dataChanged();
}

void MftReader::removeEntryByFrn(const std::wstring& volume, uint64_t frn) {
    Q_UNUSED(volume);
    QWriteLocker lock(&m_dataLock);
    
    auto it = m_frn_to_idx.find(frn);
    if (it != m_frn_to_idx.end()) {
        uint32_t idx = it->second;
        m_frns[idx] = 0; // 标记删除
        m_frn_to_idx.erase(it);

        {
            std::lock_guard<std::mutex> l(m_pathCacheMutex);
            m_path_cache.erase(frn);
        }
        emit dataChanged();
    }
}

bool MftReader::loadMftDirect(const std::wstring& volume, DriveResult& result) {
    std::wstring devPath = L"\\\\.\\" + volume;
    HANDLE hVol = CreateFileW(devPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA_V0 journal;
    DWORD cb;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journal, sizeof(journal), &cb, NULL)) {
        CloseHandle(hVol);
        return false;
    }
    result.nextUsn = journal.NextUsn;

    MFT_ENUM_DATA_V0 enumData = {0};
    enumData.HighUsn = journal.NextUsn;

    std::vector<uint8_t> buffer(1024 * 1024);
    while (DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer.data(), (DWORD)buffer.size(), &cb, NULL)) {
        if (cb < 8) break;
        uint8_t* p = buffer.data() + 8;
        uint8_t* end = buffer.data() + cb;
        while (p < end) {
            ::USN_RECORD_V2* rec = reinterpret_cast<::USN_RECORD_V2*>(p);
            RawEntry entry;
            entry.frn = rec->FileReferenceNumber;
            entry.parentFrn = rec->ParentFileReferenceNumber;
            entry.attributes = rec->FileAttributes;
            entry.modifyTime = filetimeToUnixMs(rec->TimeStamp.QuadPart);

            QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(p + rec->FileNameOffset), rec->FileNameLength / 2);
            entry.nameUtf8 = name.toUtf8().toStdString();

            result.entries.push_back(std::move(entry));
            p += rec->RecordLength;
        }
        enumData.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buffer.data());
    }

    CloseHandle(hVol);
    return !result.entries.empty();
}

void MftReader::mergeDriveResult(const std::wstring& volume, const DriveResult& result, size_t driveIdx) {
    Q_UNUSED(volume);
    size_t startIdx = m_frns.size();
    size_t count = result.entries.size();

    m_frns.reserve(startIdx + count);
    m_parent_frns.reserve(startIdx + count);
    m_sizes.reserve(startIdx + count);
    m_timestamps.reserve(startIdx + count);
    m_name_offsets.reserve(startIdx + count);
    m_attributes.reserve(startIdx + count);

    for (const auto& entry : result.entries) {
        m_frns.push_back(entry.frn);
        uint64_t encodedParent = (static_cast<uint64_t>(driveIdx) << 48) | (entry.parentFrn & 0x0000FFFFFFFFFFFFull);
        m_parent_frns.push_back(encodedParent);

        m_sizes.push_back(0);
        m_timestamps.push_back(entry.modifyTime);
        m_attributes.push_back(entry.attributes);

        m_name_offsets.push_back((uint32_t)m_string_pool.size());
        m_string_pool.insert(m_string_pool.end(), entry.nameUtf8.begin(), entry.nameUtf8.end());
        m_string_pool.push_back('\0');
    }
}

void MftReader::rebuildFrnToIndexMap() {
    m_frn_to_idx.clear();
    m_parent_to_children.clear();
    for (size_t i = 0; i < m_frns.size(); ++i) {
        if (m_frns[i] != 0) {
            m_frn_to_idx[m_frns[i]] = (uint32_t)i;
            
            uint64_t parentFrn = m_parent_frns[i] & 0x0000FFFFFFFFFFFFull;
            if (parentFrn != 0) {
                m_parent_to_children[parentFrn].push_back(m_frns[i]);
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
