#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QReadWriteLock>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <windows.h>
#include <winioctl.h>
#include "ScchCache.h"

namespace ArcMeta {

class UsnWatcher;

/**
 * @brief 高性能 MFT 索引引擎 (SoA 架构)
 */
class MftReader : public QObject {
    Q_OBJECT
public:
    static MftReader& instance();

signals:
    void dataChanged();

public:
    // 核心生命周期
    void buildIndex(const QStringList& drives = QStringList());
    bool loadFromCache();
    bool saveToCache();
    bool saveDriveToCache(size_t driveIdx);
    void clear();

    // 驱动器状态管理
    void updateActiveDrives(const QStringList& activeDrives);

    // 查询接口 (支持驱动器过滤)
    QVector<int> search(const QString& query, bool useRegex = false, bool caseSensitive = false, 
                        const QStringList& extensionList = QStringList(), 
                        bool includeHidden = true, bool includeSystem = true);
    
    QString getName(int index) const;
    int64_t getSize(int index) const;
    int64_t getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    uint64_t getFrn(int index) const;
    bool isDirectory(int index) const;
    int totalCount() const;
    QString getFullPath(int index) const;

    void updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume);
    void removeEntryByFrn(const std::wstring& volume, uint64_t frn);

private:
    std::wstring getPathFast(const std::wstring& volume, uint64_t frn);

    MftReader();
    ~MftReader();

    bool saveDriveToCacheInternal(size_t driveIdx);
    void clearInternal();
    void rebuildFrnToIndexMap();
    void buildSortedIndices();
    
    bool loadMftDirect(const std::wstring& volume, DriveResult& result);
    void mergeDriveResult(const std::wstring& volume, const DriveResult& result, size_t driveIdx);

    std::vector<uint64_t>  m_frns;
    std::vector<uint64_t>  m_parent_frns;
    std::vector<int64_t>   m_sizes;
    std::vector<int64_t>   m_timestamps;
    std::vector<uint32_t>  m_name_offsets;
    std::vector<uint32_t>  m_attributes;
    std::vector<uint8_t>   m_string_pool;

    std::vector<std::wstring> m_drive_list;
    std::vector<bool>         m_drive_active_flags; // 驱动器激活状态掩码

    std::unordered_map<uint64_t, uint32_t>              m_frn_to_idx;
    std::unordered_map<uint64_t, std::vector<uint64_t>> m_parent_to_children;

    mutable std::unordered_map<uint64_t, std::wstring>  m_path_cache;
    mutable std::mutex m_pathCacheMutex;

    std::unordered_map<std::wstring, uint64_t>          m_next_usns;
    std::vector<UsnWatcher*> m_watchers;

    mutable QReadWriteLock m_dataLock;
    bool m_isInitialized = false;
    uint32_t m_dirty_count = 0;
    std::vector<uint32_t> m_sorted_indices;
};

} // namespace ArcMeta
