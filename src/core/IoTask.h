#pragma once

#include <string>
#include <atomic>

namespace ArcMeta {

enum class TaskPriority {
    ViewportVisible = 0, // 视口可见 (最高)
    PreloadBuffer = 1,   // 预取缓冲区 (中)
    BackgroundBatch = 2  // 后台批量 (低)
};

class IoTask {
public:
    virtual ~IoTask() = default;
    virtual void run() = 0;
    virtual void cancel() { m_cancelled = true; }
    virtual bool isCancelled() const { return m_cancelled; }
    virtual std::wstring category() const { return L""; } // 例如 L"directory_scan", L"thumbnail_load", L"media_extract"
    virtual std::wstring id() const { return L""; } // 例如文件路径或目录路径
    virtual TaskPriority priority() const { return m_priority; }
    virtual void setPriority(TaskPriority priority) { m_priority = priority; }

protected:
    std::atomic<bool> m_cancelled{false};
    TaskPriority m_priority = TaskPriority::BackgroundBatch;
};

} // namespace ArcMeta
