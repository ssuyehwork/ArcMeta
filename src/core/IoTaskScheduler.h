#pragma once

#include "IoTask.h"
#include <QObject>
#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace ArcMeta {

class IoTaskScheduler : public QObject {
    Q_OBJECT
public:
    static IoTaskScheduler& instance();

    void submit(std::shared_ptr<IoTask> task);

    /**
     * @brief 动态调整任务的优先级
     */
    void updatePriority(const std::wstring& taskId, TaskPriority newPriority);

    /**
     * @brief 取消特定分类/所有未执行低优先级任务
     */
    void cancelAllByLimit(TaskPriority minPriorityToKeep);

    /**
     * @brief 根据标识符取消特定任务
     */
    void cancelTask(const std::wstring& taskId);

    /**
     * @brief 根据类别取消任务
     */
    void cancelTasksByCategory(const std::wstring& category);

    void setMaxConcurrentTasks(int maxTasks);

private:
    IoTaskScheduler();
    ~IoTaskScheduler() override;

    void workerThreadFunc();

    std::vector<std::shared_ptr<IoTask>> m_taskQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop{false};
    int m_maxConcurrent = 4;
    std::atomic<int> m_activeCount{0};
    std::vector<std::thread> m_workers;
};

} // namespace ArcMeta
