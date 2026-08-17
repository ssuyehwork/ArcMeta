#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <mutex>

namespace ArcMeta {

class VolumeOnlineManager : public QObject {
    Q_OBJECT
public:
    static VolumeOnlineManager& instance();

    // 获取当前物理在线的托管盘符集合 (如 {"C", "D", "Z"})
    QSet<QString> getOnlineDrives() const;

    // 校验特定托管库 (如 "arcmeta.library_g" 或 "G:\...") 是否处于在线状态
    bool isLibraryOnline(const QString& libraryNameOrPath) const;

    // 提取盘符 (例如 "arcmeta.library_g" -> "G", "G:/abc" -> "G", "g:" -> "G")
    static QString extractDriveLetter(const QString& str);

    // 检查并更新盘符在线状态，检测到热拔插事件时触发 volumeStateChanged 信号
    void checkVolumeState();

signals:
    // 当物理磁盘发生热拔插变更时发射广播信号 (driveLetter 为大写盘符如 "G", isOnline 为 true/false)
    void volumeStateChanged(const QString& driveLetter, bool isOnline);

private:
    explicit VolumeOnlineManager(QObject* parent = nullptr);
    ~VolumeOnlineManager() override = default;

    mutable std::mutex m_mutex;
    QSet<QString> m_onlineDrives;
    QTimer* m_timer{nullptr};
};

} // namespace ArcMeta
