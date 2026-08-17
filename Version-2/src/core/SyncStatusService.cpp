#include "SyncStatusService.h"
#include "../meta/DatabaseManager.h"
#include <QDebug>

namespace ArcMeta {

SyncStatusService& SyncStatusService::instance() {
    static SyncStatusService inst;
    return inst;
}

SyncStatusService::SyncStatusService() {
    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setInterval(150); // 150ms 节流平滑输出
    m_throttleTimer->setSingleShot(true);

    connect(m_throttleTimer, &QTimer::timeout, [this]() {
        emit statusUpdated(isSyncing(), pendingCount());
    });

    // 1. 订阅 SQLite 数据库落盘队列
    connect(&DatabaseManager::instance(), &DatabaseManager::pendingTasksCountChanged, 
            this, &SyncStatusService::updateDbPending, Qt::QueuedConnection);

    // 初始化数据库任务计数
    m_dbPending.store(DatabaseManager::instance().getPendingTasksCount());
}

int SyncStatusService::pendingCount() const {
    // 三方后台任务池总量叠加：数据库 + 多媒体提取 + 扫描
    return m_dbPending.load() + m_mediaPending.load() + m_scanPending.load();
}

void SyncStatusService::updateDbPending(int count) {
    m_dbPending.store(count);
    notifyThrottled();
}

void SyncStatusService::updateMediaPending(int count) {
    m_mediaPending.store(count);
    notifyThrottled();
}

void SyncStatusService::updateScanPending(int count) {
    m_scanPending.store(count);
    notifyThrottled();
}

void SyncStatusService::notifyThrottled() {
    if (!m_throttleTimer->isActive()) {
        m_throttleTimer->start();
    }
}

} // namespace ArcMeta
