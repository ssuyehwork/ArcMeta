#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QTimer>
#include <QFutureWatcher>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace ArcMeta {

struct ScanFilterState {
    QStringList extensionList; 
    bool useRegex = false;
    bool caseSensitive = false;
    bool includeHidden = true;
    bool includeSystem = true;

    bool isEmpty() const { 
        return extensionList.isEmpty() && !useRegex && !caseSensitive && includeHidden && includeSystem; 
    }
};

class ScanController : public QObject {
    Q_OBJECT
public:
    explicit ScanController(QObject* parent = nullptr);
    ~ScanController() override;

    void setSearchText(const QString& text);
    void setFilterState(const ScanFilterState& state);
    
    // 触发搜索（带防抖）
    void triggerSearch(bool immediate = false);

    // 排序接口（异步）
    void sort(int column, int order);

    // 结果访问 (线程安全)
    std::vector<uint64_t> results() const;
    int resultCount() const;

signals:
    void searchStarted();
    void searchFinished(int count, int64_t elapsedMs);
    
    // 2026-06-xx 响应式信号
    void resultsSwapped();
    void entryAdded(uint64_t key, int row);
    void entryRemoved(uint64_t key, int row);
    void entryUpdated(uint64_t key, int row);

private slots:
    void onMftEntryAdded(uint64_t key);
    void onMftEntryRemoved(uint64_t key);
    void onMftEntryUpdated(uint64_t key);

private:
    void performSearch();

    QString m_searchText;
    ScanFilterState m_filterState;
    std::vector<uint64_t> m_results;
    std::unordered_set<uint64_t> m_resultsSet; // 用于 O(1) 存在性检查
    mutable std::mutex m_resultsMutex;
    
    QTimer* m_debounceTimer = nullptr;
    QFutureWatcher<std::vector<uint64_t>> m_watcher;
    QFutureWatcher<std::vector<uint64_t>> m_sortWatcher;
};

} // namespace ArcMeta
