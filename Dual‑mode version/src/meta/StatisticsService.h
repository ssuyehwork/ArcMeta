#pragma once 
#include <QObject> 
#include <QMap> 
#include <QString> 
#include <QTimer>
#include <atomic> 
#include <mutex> 
#include <functional> 
#include "CategoryRepo.h" // 复用 StatisticsSnapshot 结构体 

namespace ArcMeta { 
class StatisticsService : public QObject { 
    Q_OBJECT 
public: 
    static StatisticsService& instance(); 

    // 1. 0ms 纯内存读取（UI 刷新时即时拉取当前已缓存的账本） 
    StatisticsSnapshot getCachedSnapshot() const; 

    // 2. 异步重新核算全量账本（后台线程池运行，算完自动发射 statisticsUpdated 信号） 
    void requestFullRecountAsync(std::function<void(const StatisticsSnapshot&)> callback = nullptr); 

    // 3. 增量变更接口（由托管生命周期服务单向驱动原子计数） 
    void notifyAssetAdded(int targetCatId, bool hasTags); 
    void notifyAssetRemoved(int targetCatId, int libraryCatId, bool hadTags, bool wasTrash); 
    void purgeAsset(int libraryCatId, const std::vector<int>& userCatIds, bool hasTags, bool isTrash);
    void notifyAssetTrashChanged(bool toTrash, bool hasTags); 
    void notifyDiskTrashCountChanged(int delta); 

signals: 
    void statisticsUpdated(const StatisticsSnapshot& snapshot); 

private: 
    StatisticsService(QObject* parent = nullptr); 
public:
    StatisticsSnapshot computeSnapshotFromDb(); // 仅在 QThreadPool 线程中执行 SQL 

    mutable std::mutex m_snapshotMutex; 
    StatisticsSnapshot m_cachedSnapshot; 

    std::atomic<int> m_totalCount{0}; 
    std::atomic<int> m_uncategorizedCount{0}; 
    std::atomic<int> m_untaggedCount{0}; 
    std::atomic<int> m_trashCount{0}; 

    // 增量账本分类与标签映射表
    std::unordered_map<int, int> m_categoryCounts;
    std::unordered_map<int, int> m_tagCounts;

    QTimer* m_debounceTimer{nullptr};
}; 
} 
