#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <QIcon>
#include <QHash>
#include "ScchCache.h"
#include "LruCache.h"

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

class UsnWatcher;

/**
 * @brief 高性能 MFT 索引引擎 (SoA 架构)
 */
class MftReader : public QObject {
    Q_OBJECT
public:
    static MftReader& instance();

    // 全局图标缓存管理 (解决 UAF 风险)
    QIcon getCachedIcon(const QString& ext, bool isDir);

signals:
    void dataChanged(int index = -1);

public:
    // 生命周期管理
    void buildIndex(const QStringList& drives = QStringList());
    bool loadFromCache();
    bool saveToCache(); 
    bool saveDriveToCache(size_t driveIdx); 
    void clear();

    // 驱动器隔离状态管理
    void updateActiveDrives(const QStringList& activeDrives);

    // 查询接口 (支持驱动器掩码隔离)
    QVector<int> search(const QString& query, bool useRegex = false, bool caseSensitive = false, 
                        const QStringList& extensionList = QStringList(), 
                        bool includeHidden = true, bool includeSystem = true);
    
    // SoA 访问接口 (支持索引重定向，处理 Overlay 覆盖层)
    QString getName(int index) const;
    int64_t getSize(int index) const;
    int64_t getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    uint64_t getFrn(int index) const;
    bool isDirectory(int index) const;
    int totalCount() const;
    QString getFullPath(int index) const;
    void requestMetadata(int index);
    bool isMetadataFetched(int index) const;

    // USN 更新
    void updateEntryFromUsn(USN_RECORD_V2* record, const std::wstring& volume);
    void removeEntryByFrn(const std::wstring& volume, uint64_t frn);

private:
    std::wstring getPathFast(const std::wstring& volume, uint64_t frn);

    MftReader();
    ~MftReader();

    // 内部结构体
    struct RawEntry {
        uint64_t frn;
        uint64_t parentFrn;
        uint64_t size;
        uint32_t attributes;
        int64_t  modifyTime;
        std::string nameUtf8;
    };

    struct OverlayEntry {
        uint64_t frn;
        uint64_t parentFrn;
        uint64_t size;
        uint32_t attributes;
        int64_t  modifyTime;
        std::string nameUtf8;
        bool deleted = false;
    };
    struct DriveResult {
        std::vector<RawEntry> entries;
        uint64_t nextUsn;
    };

    bool saveDriveToCacheInternal(size_t driveIdx); 
    void clearInternal(); 
    void rebuildFrnToIndexMap();
    void compact();
    void buildSortedIndices();
    
    bool loadMftDirect(const std::wstring& volume, DriveResult& result);
    void mergeDriveResult(const std::wstring& volume, const DriveResult& result, size_t driveIdx);

    // SoA 视图层 (零拷贝引用)
    struct DriveView {
        const uint64_t* frns = nullptr;
        const uint64_t* parent_frns = nullptr;
        const int64_t*  sizes = nullptr;
        const int64_t*  timestamps = nullptr;
        const uint32_t* name_offsets = nullptr;
        const uint32_t* attributes = nullptr;
        const uint8_t*  metadata_fetched = nullptr;
        const uint8_t*  string_pool = nullptr;
        const uint32_t* sorted_indices = nullptr;
        size_t          count = 0;
        size_t          pool_size = 0;
        size_t          drive_idx = 0;
        uint32_t        global_offset = 0; // 该驱动器在全局索引中的起始偏移
    };
    std::vector<DriveView> m_drive_views;

    // SoA 主数据 (仅用于新扫描的数据)
    std::vector<uint64_t>  m_frns;
    std::vector<uint64_t>  m_parent_frns;
    std::vector<int64_t>   m_sizes;
    std::vector<int64_t>   m_timestamps;   
    std::vector<uint32_t>  m_name_offsets;
    std::vector<uint32_t>  m_attributes;
    std::vector<uint8_t>   m_metadata_fetched;
    std::vector<uint8_t>   m_string_pool;

    std::vector<std::wstring> m_drive_list;
    std::atomic<uint32_t>     m_drive_active_mask{0}; // 驱动器过滤掩码 (位图)

    std::unordered_map<uint64_t, uint32_t>              m_frn_to_idx;

    mutable LruCache<uint64_t, std::wstring>            m_path_cache;

    std::unordered_map<std::wstring, uint64_t>          m_next_usns;
    std::vector<std::unique_ptr<ScchMmapData>> m_mmap_datas;
    std::vector<UsnWatcher*> m_watchers;

    // Overlay 覆盖层模型：用于实时变动，保持主 SoA 索引只读
    mutable QReadWriteLock m_overlayLock;
    std::unordered_map<uint64_t, OverlayEntry> m_overlay;
    std::vector<uint64_t> m_overlay_frns; // 辅助顺序访问

    mutable QReadWriteLock m_dataLock;
    mutable QReadWriteLock m_iconCacheLock;
    QHash<QString, QIcon>  m_icon_cache;

    bool m_isInitialized = false;
    uint32_t m_dirty_count = 0;
    size_t   m_dead_count = 0;
    size_t   m_wasted_string_bytes = 0;
    std::vector<uint32_t> m_sorted_indices;
};

} // namespace ArcMeta
