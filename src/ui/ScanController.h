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

    // 结果访问
    const std::vector<uint64_t>& results() const { return m_results; }
    int resultCount() const { return static_cast<int>(m_results.size()); }

signals:
    void searchStarted();
    void searchFinished(int count, int64_t elapsedMs);

private:
    void performSearch();

    QString m_searchText;
    ScanFilterState m_filterState;
    std::vector<uint64_t> m_results;
    
    QTimer* m_debounceTimer = nullptr;
    QFutureWatcher<std::vector<uint64_t>> m_watcher;
};

} // namespace ArcMeta
