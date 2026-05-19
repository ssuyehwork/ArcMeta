#include "ScanController.h"
#include "../mft/MftReader.h"
#include <QtConcurrent>
#include <QElapsedTimer>

namespace ArcMeta {

ScanController::ScanController(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(300); // 300ms 黄金防抖时间

    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        performSearch();
    });

    connect(&m_watcher, &QFutureWatcher<std::vector<uint64_t>>::finished, this, [this]() {
        if (m_watcher.isCanceled()) return;

        m_results = m_watcher.result();
        // 这里可以记录耗时
        emit searchFinished(static_cast<int>(m_results.size()), 0 /* TODO: pass elapsed */);
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
        m_results = m_watcher.result();
        emit searchFinished(static_cast<int>(m_results.size()), timer.elapsed());
    });

    m_watcher.setFuture(future);
}

} // namespace ArcMeta
