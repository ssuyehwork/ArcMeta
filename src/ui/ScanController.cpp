#include "ScanController.h"
#include "../mft/MftReader.h"
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>

namespace ArcMeta {

ScanController::ScanController(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(300); // 300ms 黄金防抖时间

    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        performSearch();
    });

    auto& reader = MftReader::instance();
    connect(&reader, &MftReader::entryAdded, this, &ScanController::onMftEntryAdded);
    connect(&reader, &MftReader::entryRemoved, this, &ScanController::onMftEntryRemoved);
    connect(&reader, &MftReader::entryUpdated, this, &ScanController::onMftEntryUpdated);

    connect(&m_sortWatcher, &QFutureWatcher<std::vector<uint64_t>>::finished, this, [this]() {
        if (m_sortWatcher.isCanceled()) return;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results = m_sortWatcher.result();
            m_resultsSet.clear();
            m_resultsSet.insert(m_results.begin(), m_results.end());
        }
        emit resultsSwapped();
    });
}

ScanController::~ScanController() {
    m_watcher.cancel();
    m_watcher.waitForFinished();
}

void ScanController::setSearchText(const QString& text) {
    if (m_searchText == text) return;
    m_searchText = text;
}

void ScanController::setFilterState(const ScanFilterState& state) {
    // 简单比对逻辑省略，直接赋值
    m_filterState = state;
}

void ScanController::triggerSearch(bool immediate) {
    if (immediate) {
        m_debounceTimer->stop();
        performSearch();
    } else {
        m_debounceTimer->start();
    }
}

std::vector<uint64_t> ScanController::results() const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

int ScanController::resultCount() const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return static_cast<int>(m_results.size());
}

void ScanController::performSearch() {
    if (m_watcher.isRunning()) m_watcher.cancel();

    emit searchStarted();
    
    QElapsedTimer timer;
    timer.start();

    auto future = QtConcurrent::run([text = m_searchText, state = m_filterState]() {
        return MftReader::instance().search(text, state.useRegex, state.caseSensitive, state.extensionList, state.includeHidden, state.includeSystem);
    });

    disconnect(&m_watcher, &QFutureWatcher<std::vector<uint64_t>>::finished, this, nullptr);
    connect(&m_watcher, &QFutureWatcher<std::vector<uint64_t>>::finished, this, [this, timer]() {
        if (m_watcher.isCanceled()) return;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results = m_watcher.result();
            m_resultsSet.clear();
            m_resultsSet.insert(m_results.begin(), m_results.end());
        }
        emit searchFinished(static_cast<int>(m_results.size()), timer.elapsed());
    });

    m_watcher.setFuture(future);
}

void ScanController::sort(int column, int order) {
    if (m_sortWatcher.isRunning()) m_sortWatcher.cancel();

    auto future = QtConcurrent::run([keys = m_results, column, order]() mutable {
        auto& reader = MftReader::instance();
        std::sort(keys.begin(), keys.end(), [&](uint64_t a, uint64_t b) {
            int idxA = reader.getIndexByKey(a);
            int idxB = reader.getIndexByKey(b);
            if (idxA == -1 || idxB == -1) return false;

            bool less = false;
            switch (column) {
                case 0: less = QString::compare(reader.getName(idxA), reader.getName(idxB), Qt::CaseInsensitive) < 0; break;
                case 1: less = QString::compare(reader.getFullPath(idxA), reader.getFullPath(idxB), Qt::CaseInsensitive) < 0; break;
                case 2: less = reader.getSize(idxA) < reader.getSize(idxB); break;
                case 3: less = reader.getModifyTime(idxA) < reader.getModifyTime(idxB); break;
                default: return false;
            }
            return (order == 0 /* Qt::AscendingOrder */) ? less : !less;
        });
        return keys;
    });

    m_sortWatcher.setFuture(future);
}

void ScanController::onMftEntryAdded(uint64_t key) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    if (m_resultsSet.count(key)) return;

    int idx = MftReader::instance().getIndexByKey(key);
    if (idx == -1) return;

    if (MftReader::instance().matchEntry(idx, m_searchText, m_filterState.useRegex, m_filterState.caseSensitive, 
                                        m_filterState.extensionList, m_filterState.includeHidden, m_filterState.includeSystem)) {
        int row = static_cast<int>(m_results.size());
        m_results.push_back(key);
        m_resultsSet.insert(key);
        emit entryAdded(key, row);
    }
}

void ScanController::onMftEntryRemoved(uint64_t key) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    auto it = std::find(m_results.begin(), m_results.end(), key);
    if (it != m_results.end()) {
        int row = static_cast<int>(std::distance(m_results.begin(), it));
        m_results.erase(it);
        m_resultsSet.erase(key);
        emit entryRemoved(key, row);
    }
}

void ScanController::onMftEntryUpdated(uint64_t key) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    auto it = std::find(m_results.begin(), m_results.end(), key);
    int idx = MftReader::instance().getIndexByKey(key);
    
    bool matches = (idx != -1) && MftReader::instance().matchEntry(idx, m_searchText, m_filterState.useRegex, m_filterState.caseSensitive, 
                                                                  m_filterState.extensionList, m_filterState.includeHidden, m_filterState.includeSystem);

    if (it != m_results.end()) {
        int row = static_cast<int>(std::distance(m_results.begin(), it));
        if (matches) {
            emit entryUpdated(key, row);
        } else {
            m_results.erase(it);
            m_resultsSet.erase(key);
            emit entryRemoved(key, row);
        }
    } else if (matches) {
        int row = static_cast<int>(m_results.size());
        m_results.push_back(key);
        m_resultsSet.insert(key);
        emit entryAdded(key, row);
    }
}

} // namespace ArcMeta
