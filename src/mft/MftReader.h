#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QReadWriteLock>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <windows.h>
#include "../core/IndexedEntry.h"

namespace ArcMeta {

class UsnWatcher;
struct UsnChange; // 前向声明 UsnChange

// 2026-05-10 对标 Rust：引入轻量级线程安全 LRU 缓存，用于极速路径解析
class LruPathCache {
public:
    explicit LruPathCache(size_t capacity) : m_capacity(capacity) {}

    bool tryGet(int index, QString& outPath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(index);
        if (it == m_map.end()) return false;
        m_list.splice(m_list.begin(), m_list, it->second.second);
        outPath = it->second.first;
        return true;
    }

    void put(int index, const QString& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(index);
        if (it != m_map.end()) {
            m_list.splice(m_list.begin(), m_list, it->second.second);
            it->second.first = path;
            return;
        }
        if (m_map.size() >= m_capacity) {
            int old = m_list.back();
            m_list.pop_back();
            m_map.erase(old);
        }
        m_list.push_front(index);
        m_map[index] = {path, m_list.begin()};
    }

    void invalidate(int index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(index);
        if (it != m_map.end()) {
            m_list.erase(it->second.second);
            m_map.erase(it);
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map.clear();
        m_list.clear();
    }

private:
    size_t m_capacity;
    std::mutex m_mutex;
    std::list<int> m_list;
    std::unordered_map<int, std::pair<QString, std::list<int>::iterator>> m_map;
};

// 2026-05-10 对标 Rust：增量覆盖层机制，实现无锁 USN 变更即时反映
struct OverlayEntry {
    QString name;
    int64_t size = 0;
    int64_t modifyTime = 0;
    uint32_t attributes = 0;
    unsigned __int64 parentFrn = 0;
    bool isDeleted = false;
    bool isDir = false;
};

/**
 * @brief 高性能 MFT 索引引擎 (2026-05-09 极致重构)
 * 
 * 采用 SoA (Structure of Arrays) 架构，彻底取代旧版 unordered_map 逻辑。
 * 实现百万级文件秒级扫描、零拷贝加载与实时监控。
 */
class MftReader : public QObject {
    Q_OBJECT
public:
    static MftReader& instance();

signals:
    void dataChanged(); // 2026-05-11 新增：USN 变更通知信号

public:
    // 核心生命周期
    void buildIndex(const QStringList& drives = QStringList());
    bool loadFromCache();
    bool saveToCache(); // 2026-05-11 新增：SoA 持久化接口
    void clear(); // 释放所有内存资源

    // 极致查询接口 (SoA 并行分块规约)
    QVector<int> search(const QString& query, bool useRegex = false, bool caseSensitive = false, const QStringList& extensionList = QStringList(), bool includeHidden = true, bool includeSystem = true);
    
    // 2026-05-10 对标 Rust：前缀极速查找 (二分法)
    QVector<int> searchPrefix(const QString& prefix);

    // USN 覆盖层同步接口
    void applyChanges(const QList<UsnChange>& changes);

    // SoA 数据访问 (支持 Overlay 虚拟索引)
    QString getName(int index) const;
    int64_t getSize(int index) const;
    int64_t getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    unsigned __int64 getFrn(int index) const;
    bool isDirectory(int index) const;
    int totalCount() const;
    QString getFullPath(int index) const;


private:
    MftReader();
    ~MftReader();

    // 内部扫描逻辑 (移植自 ScanDialog 的高性能实现)
    struct DriveResult {
        std::vector<IndexedEntry> entries;
        QHash<QString, int> typeCounts;
        QHash<QString, int> suffixCounts;
    };
    DriveResult performMftScan(const QString& driveRoot);
    
    // 辅助方法
    QStringList getAvailableDrives() const;
    bool isFixedDrive(const QString& drive) const;
    void mergeDriveResult(const DriveResult& result);
    void rebuildFrnToIndexMap();
    void buildSortedIndices();
    QString buildFullPath(int index) const;
    unsigned __int64 getParentFrn(int index) const;
    bool loadMftDirect(const std::wstring& volumePath, DriveResult& result);
    void scanDirectoryFallback(const std::wstring& volumePath, DriveResult& result);

    // 2026-05-10 对标 Rust：纯FRN SoA架构 (Structure of Arrays)
    std::vector<uint64_t> m_frns;           // FRN数组
    std::vector<uint64_t> m_parent_frns;     // 父FRN数组  
    std::vector<uint64_t> m_sizes;           // 文件大小
    std::vector<uint64_t> m_timestamps;      // 修改时间戳
    std::vector<uint32_t> m_name_offsets;    // 名称在字符串池中的偏移
    std::vector<uint32_t> m_attributes;      // 文件属性
    std::vector<uint8_t>  m_string_pool;      // 单一连续字符串池
    
    // 快速索引 (对标 Rust)
    std::unordered_map<uint64_t, uint32_t> m_frn_to_idx; // FRN -> 索引映射
    mutable QReadWriteLock m_dataLock;

    // 监控器管理
    QVector<UsnWatcher*> m_watchers;
    QHash<QString, unsigned __int64> m_nextUsns;

    bool m_isInitialized = false;

    // 2026-05-10 对标 Rust：前缀二分查找所需的排序索引
    std::vector<uint32_t> m_sorted_indices; // 排序后的索引数组

    // 2026-05-10 对标 Rust：Overlay 覆盖层与 LRU 缓存
    std::unordered_map<unsigned __int64, OverlayEntry> m_overlay; // FRN -> 变更条目
    std::unordered_map<int, unsigned __int64> m_indexToOverlayFrn; // 将新条目映射为一个伪索引 (负数)
    int m_nextVirtualIndex = -2; // 虚拟索引从 -2 开始递减
    
    mutable LruPathCache m_pathCache{20000}; // 2万条目的路径 LRU 缓存
    std::vector<int> m_sharedIndices; // 常驻内存复用
    int m_dirtyCount = 0;
};

} // namespace ArcMeta
