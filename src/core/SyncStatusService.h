#ifndef QuarkMeta_SYNC_STATUS_SERVICE_H
#define QuarkMeta_SYNC_STATUS_SERVICE_H

#include <QObject>
#include <QTimer>
#include <atomic>

namespace QuarkMeta {

/**
 * @brief 同步状态服务 (多源任务聚合重构)
 * 负责从底层数据库落盘、文件扫描以及多媒体特征解析提取接收高频信号并进行节流分发。
 */
class SyncStatusService : public QObject {
    Q_OBJECT
public:
    static SyncStatusService& instance();

    /**
     * @brief 是否正在同步/扫描中 (线程安全)
     */
    bool isSyncing() const { return pendingCount() > 0; }

    /**
     * @brief 获取待处理总任务数 (线程安全)
     */
    int pendingCount() const;

public slots:
    // 供各后台模块投递待处理任务计数的统一槽方法
    void updateDbPending(int count);
    void updateMediaPending(int count);
    void updateScanPending(int count);

signals:
    /**
     * @brief 节流后的状态更新信号 (主线程触发)
     */
    void statusUpdated(bool syncing, int pendingCount);

private:
    SyncStatusService();
    ~SyncStatusService() override = default;

    void notifyThrottled();

    QTimer* m_throttleTimer = nullptr;
    std::atomic<int> m_dbPending{0};    // 数据库待刷盘任务数
    std::atomic<int> m_mediaPending{0}; // 多媒体特征待提取项数
    std::atomic<int> m_scanPending{0};  // 后台文件扫描待注册项数
};

} // namespace QuarkMeta

#endif // QuarkMeta_SYNC_STATUS_SERVICE_H
