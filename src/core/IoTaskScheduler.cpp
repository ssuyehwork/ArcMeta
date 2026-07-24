#include "IoTaskScheduler.h"
#include <algorithm>
#include <QDebug>

namespace ArcMeta {

IoTaskScheduler& IoTaskScheduler::instance() {
    static IoTaskScheduler inst;
    return inst;
}

IoTaskScheduler::IoTaskScheduler() {
    // 默认并发上限设为系统的处理器核心数或 4，I/O 密集型不建议过多
    unsigned int cores = std::thread::hardware_concurrency();
    m_maxConcurrent = cores > 0 ? std::clamp(cores, 2u, 8u) : 4;

    for (int i = 0; i < m_maxConcurrent; ++i) {
        m_workers.emplace_back(&IoTaskScheduler::workerThreadFunc, this);
    }
    qDebug() << "[Scheduler] 统一任务调度层已启动，工作线程数:" << m_maxConcurrent;
}

IoTaskScheduler::~IoTaskScheduler() {
    m_stop = true;
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void IoTaskScheduler::submit(std::shared_ptr<IoTask> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskQueue.push_back(task);
    }
    m_cv.notify_one();
}

void IoTaskScheduler::updatePriority(const std::wstring& taskId, TaskPriority newPriority) {
    if (taskId.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& task : m_taskQueue) {
        if (task->id() == taskId) {
            task->setPriority(newPriority);
        }
    }
}

void IoTaskScheduler::cancelAllByLimit(TaskPriority minPriorityToKeep) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ) {
        // 如果任务优先级低于保留阈值，则主动取消并移除
        if (static_cast<int>((*it)->priority()) > static_cast<int>(minPriorityToKeep)) {
            (*it)->cancel();
            it = m_taskQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void IoTaskScheduler::cancelTask(const std::wstring& taskId) {
    if (taskId.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ) {
        if ((*it)->id() == taskId) {
            (*it)->cancel();
            it = m_taskQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void IoTaskScheduler::cancelTasksByCategory(const std::wstring& category) {
    if (category.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ) {
        if ((*it)->category() == category) {
            (*it)->cancel();
            it = m_taskQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void IoTaskScheduler::setMaxConcurrentTasks(int maxTasks) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxConcurrent = maxTasks;
    // 运行时不轻易销毁已有 thread 避免死锁，主要由 workerThreadFunc 的并发上限决定
}

void IoTaskScheduler::workerThreadFunc() {
    while (!m_stop) {
        std::shared_ptr<IoTask> taskToRun;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return m_stop || (!m_taskQueue.empty() && m_activeCount < m_maxConcurrent);
            });

            if (m_stop) break;

            if (!m_taskQueue.empty() && m_activeCount < m_maxConcurrent) {
                // 找到优先级最高的任务（ViewportVisible=0 优先级最高）
                auto bestIt = m_taskQueue.begin();
                for (auto it = m_taskQueue.begin(); it != m_taskQueue.end(); ++it) {
                    if (static_cast<int>((*it)->priority()) < static_cast<int>((*bestIt)->priority())) {
                        bestIt = it;
                    }
                }

                taskToRun = *bestIt;
                m_taskQueue.erase(bestIt);
                m_activeCount++;
            }
        }

        if (taskToRun) {
            if (!taskToRun->isCancelled()) {
                try {
                    taskToRun->run();
                } catch (const std::exception& e) {
                    qWarning() << "[Scheduler] 任务运行异常:" << e.what();
                } catch (...) {
                    qWarning() << "[Scheduler] 任务运行未知异常";
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_activeCount--;
                m_cv.notify_all(); // 锁内安全通知，完全规避 Lost Wakeup 与 C++ 内存模型下的极低概率竞态
            }
        }
    }
}

} // namespace ArcMeta
