#pragma once

#include <QThread>
#include <string>
#include <atomic>
#include <windows.h>

namespace ArcMeta {

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

protected:
    void run() override;

private:
    void handleRecord(USN_RECORD_V2* pRecord);

    std::wstring m_volume; // e.g. L"C:"
    uint64_t m_lastUsn;
    std::atomic<bool> m_stopRequested;
    HANDLE m_hVolume;
};

} // namespace ArcMeta
