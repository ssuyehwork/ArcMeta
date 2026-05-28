#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <windows.h>
#include <winioctl.h>
#include <QThread>
#include <QByteArray>

namespace ArcMeta {

/**
 * @brief 重构后的 USN 监控器
 * 使用 Overlapped I/O 配合事件通知，实现真正的异步非阻塞监听。
 */
class UsnWatcher : public QThread {
    Q_OBJECT
public:
    explicit UsnWatcher(const std::wstring& volume, uint64_t startUsn = 0, QObject* parent = nullptr);
    virtual ~UsnWatcher();

    void stop();

signals:
    // 2026-06-xx 同步：确保信号在头文件中被正确声明
    void recordReceived(const QByteArray& recordData);
    void journalInvalidated();

protected:
    void run() override;

private:
    std::wstring m_volume;
    uint64_t m_lastUsn;
    std::atomic<bool> m_stopRequested;

    // 物理补全：Windows 事件句柄与卷句柄
    HANDLE m_hVolume = INVALID_HANDLE_VALUE;
    HANDLE m_hEvent = NULL;
};

} // namespace ArcMeta
