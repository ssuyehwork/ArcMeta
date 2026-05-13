#pragma once

#include <QString>
#include <QThread>
#include <QList>
#include <atomic>
#include <windows.h>

namespace ArcMeta {

// 变动记录结构
struct UsnChange {
    enum Type { Created, Deleted, Renamed, Modified };
    Type type;
    unsigned __int64 frn;
    unsigned __int64 parentFrn;
    QString name;
    uint32_t attributes;
    long long size;
};

/**
 * @brief 高性能 USN 日志监控器 (2026-05-09 重构)
 * 
 * 实时监控 NTFS 卷的文件变动，对标 Rust 版本的 spawn_usn_watcher。
 * 统一作为项目中唯一的 USN 监控组件。
 */
class UsnWatcher : public QThread {
    Q_OBJECT
public:
    // 支持 2 参数 (MftReader) 和 3 参数 (ScanDialog) 调用
    explicit UsnWatcher(const QString& volume, unsigned __int64 startUsn = 0, QObject* parent = nullptr);
    virtual ~UsnWatcher();

    void stop();

signals:
    void changesDetected(const QList<UsnChange>& changes);

protected:
    void run() override;

private:
    void handleLegacyRecord(struct _USN_RECORD_V2* record); // 兼容旧逻辑的内部处理(可选)

    QString m_volume;
    unsigned __int64 m_startUsn;
    std::atomic<bool> m_stopRequested;
    HANDLE m_hVolume;
};

} // namespace ArcMeta
