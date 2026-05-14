#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QReadWriteLock>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <winioctl.h>
#include "ScchCache.h"
#include "UsnWatcher.h"

namespace ArcMeta {

/**
 * @brief 最终合并版 MftReader
 * 
 * 集成 SoA 架构、.scch 缓存与 UsnWatcher 实时更新。
 */
class MftReader : public QObject {
    Q_OBJECT
public:
    static MftReader& instance();

    // 生命周期与持久化
    void buildIndex(const QStringList& drives = QStringList());
    bool loadFromCache();
    bool saveToCache();
    void clear();

    // 数据查询
    QVector<int> search(const QString& query, bool useRegex = false, bool caseSensitive = false, const QStringList& extensionList = QStringList(), bool includeHidden = true, bool includeSystem = true);
    QVector<int> searchPrefix(const QString& prefix);

    // 路径重建
    std::wstring getPathFast(const std::wstring& volume, uint64_t frn);
    QString getFullPath(int index) const;

    // SoA 数据访问
    QString getName(int index) const;
    int64_t getSize(int index) const;
    int64_t getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    uint64_t getFrn(int index) const;
    bool isDirectory(int index) const;
    int totalCount() const;

    // USN 更新接口
    void updateEntryFromUsn(USN_RECORD_V2* pRecord, const std::wstring& volume);
    void removeEntryByFrn(const std::wstring& volume, uint64_t frn);
    void applyChanges(const QList<UsnChange>& changes);

signals:
    void dataChanged();

private:
    MftReader();
    ~MftReader();

    void clearInternal();
    void rebuildFrnToIndexMap();
    void buildSortedIndices();

    struct RawEntry {
        uint64_t frn;
        uint64_t parentFrn;
        uint32_t attributes;
        int64_t modifyTime;
        int64_t size;
        std::string nameUtf8;
    };
    struct DriveResult {
        std::wstring volume;
        std::vector<RawEntry> entries;
    };
    bool loadMftDirect(const std::wstring& volumePath, DriveResult& result);
    void mergeDriveResult(const DriveResult& result);
    
    QStringList getAvailableDrives() const;
    bool isFixedDrive(const QString& drive) const;

    // SoA 主数据
    std::vector<uint64_t>      m_frns;
    std::vector<uint64_t>      m_parent_frns;
    std::vector<int64_t>       m_sizes;
    std::vector<int64_t>       m_timestamps;   // Unix 毫秒
    std::vector<uint32_t>      m_name_offsets;
    std::vector<uint32_t>      m_attributes;
    std::vector<uint8_t>       m_string_pool;
    std::vector<uint16_t>      m_drive_indices; // 对应 m_drive_list 的索引

    // 盘符列表 (如 L"C:")
    std::vector<std::wstring>  m_drive_list;

    // 反向索引
    std::unordered_map<uint64_t, uint32_t>              m_frn_to_idx;
    std::unordered_map<uint64_t, std::vector<uint64_t>> m_parent_to_children;

    // 路径缓存
    mutable std::unordered_map<uint64_t, std::wstring>  m_path_cache;

    // USN 水位线（盘符 -> NextUsn）
    std::unordered_map<std::wstring, uint64_t>          m_next_usns;

    // 并发控制
    mutable QReadWriteLock m_dataLock;
    bool m_isInitialized = false;

    // 监控器管理
    std::vector<UsnWatcher*> m_watchers;
    
    // 排序索引
    std::vector<uint32_t> m_sorted_indices;
};

} // namespace ArcMeta
