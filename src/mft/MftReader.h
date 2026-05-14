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
    bool saveToCache(); // 保存所有已加载的盘符
    bool saveDriveToCache(size_t driveIdx); // 保存特定盘符
    void clear();

    // 查询接口
    QVector<int> search(const QString& query, bool useRegex = false, bool caseSensitive = false, 
                        const QStringList& extensionList = QStringList(), 
                        bool includeHidden = true, bool includeSystem = true);
    
    // SoA 数据访问
    QString getName(int index) const;
    int64_t getSize(int index) const;
    int64_t getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    uint64_t getFrn(int index) const;
    bool isDirectory(int index) const;
    int totalCount() const;
    QString getFullPath(int index) const;

    // USN 实时更新接口 (由 UsnWatcher 调用)
    void updateEntryFromUsn(::USN_RECORD_V2* record, const std::wstring& volume);
    void removeEntryByFrn(const std::wstring& volume, uint64_t frn);

    // 路径重建 (含盘符注入)
    std::wstring getPathFast(const std::wstring& volume, uint64_t frn);

private:
    MftReader();
    ~MftReader();

    bool saveDriveToCacheInternal(size_t driveIdx); // 内部无锁版本

    void clearInternal(); // 内部无锁版本
    void rebuildFrnToIndexMap();
    void buildSortedIndices();
    
    // MFT 扫描辅助
    struct RawEntry {
        uint64_t frn;
        uint64_t parentFrn;
        uint32_t attributes;
        int64_t  modifyTime;
        std::string nameUtf8;
    };
    struct DriveResult {
        std::vector<RawEntry> entries;
        uint64_t nextUsn;
    };
    bool loadMftDirect(const std::wstring& volume, DriveResult& result);
    void mergeDriveResult(const std::wstring& volume, const DriveResult& result, size_t driveIdx);

    // SoA 主数据
    std::vector<uint64_t>  m_frns;
    std::vector<uint64_t>  m_parent_frns; // 高 16 位存储盘符索引
    std::vector<int64_t>   m_sizes;
    std::vector<int64_t>   m_timestamps;   // Unix 毫秒
    std::vector<uint32_t>  m_name_offsets;
    std::vector<uint32_t>  m_attributes;
    std::vector<uint8_t>   m_string_pool;

    // 盘符列表 (用于编码/解码)
    std::vector<std::wstring> m_drive_list;

    // 反向索引
    std::unordered_map<uint64_t, uint32_t>              m_frn_to_idx;
    std::unordered_map<uint64_t, std::vector<uint64_t>> m_parent_to_children;

    // 路径缓存与互斥锁
    mutable std::unordered_map<uint64_t, std::wstring>  m_path_cache;
    mutable std::mutex m_pathCacheMutex;

    // USN 水位线
    std::unordered_map<std::wstring, uint64_t>          m_next_usns;

    // 监控器管理
    std::vector<UsnWatcher*> m_watchers;

    mutable QReadWriteLock m_dataLock;
    bool m_isInitialized = false;
    uint32_t m_dirty_count = 0;

    // 排序索引
    std::vector<uint32_t> m_sorted_indices;
};

} // namespace ArcMeta
