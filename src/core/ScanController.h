#pragma once

#include <QObject>
#include <QString>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QPushButton>
#include <memory>
#include <atomic>

namespace ArcMeta {

struct ScanConfig {
    QSet<QString> activeDrives;
    QSet<QString> defaultDrives;
    QSet<QString> ignoredDrives;
    QStringList queryHistory;
    QStringList extHistory;

    int viewMode = 0;   // 0: Details, 1: Icons
    int iconSize = 128; // 256, 128, 64
    int sortColumn = 0;
    int sortOrder = 0;  // 0: Asc, 1: Desc

    void load();
    void save();
};

struct DriveInfo {
    QString letter;
    QString label;
    bool isNtfs;
    bool hasMedia;
};

/**
 * @brief 扫描控制器
 * 负责驱动探测、扫描任务调度、配置管理，实现 UI 与逻辑解耦。
 */
class ScanController : public QObject {
    Q_OBJECT
public:
    static ScanController& instance();

    void loadConfig();
    void saveConfig();
    ScanConfig& config() { return m_config; }

    void requestDriveProbe(bool force = false);
    void requestScan(const QStringList& drives);
    void updateActiveDrives(const QStringList& drives);

    QVector<DriveInfo> cachedDriveInfos() const { return m_cachedDriveInfos; }

signals:
    void driveProbeFinished(const QVector<DriveInfo>& drives);
    void scanStarted();
    void scanFinished(bool success);
    void statusUpdated(const QString& text, bool isScanning);

private:
    ScanController(QObject* parent = nullptr);
    ~ScanController() override;

    ScanConfig m_config;
    QVector<DriveInfo> m_cachedDriveInfos;
};

} // namespace ArcMeta
