#pragma once

#include <windows.h>
#include <winioctl.h>
#include <QThread>
#include <QString>
#include <QList>
#include <string>
#include <atomic>

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
 * @brief 最终合并版 UsnWatcher
 * 
 * 实时监控卷变动，并调用 MftReader 接口更新内存 SoA。
 */
class UsnWatcher : public QThread {
    Q_OBJECT
public:
    explicit UsnWatcher(const std::wstring& volume, uint64_t startUsn = 0, QObject* parent = nullptr);
    virtual ~UsnWatcher();

    void stop();

signals:
    void changesDetected(const QList<UsnChange>& changes);

protected:
    void run() override;

private:
    void handleRecord(::USN_RECORD_V2* pRecord);

    std::wstring m_volume; // e.g. L"C:"
    uint64_t m_lastUsn;
    std::atomic<bool> m_stopRequested;
    HANDLE m_hVolume;
};

} // namespace ArcMeta
