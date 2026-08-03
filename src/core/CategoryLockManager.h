#pragma once

#include <QSet>
#include <mutex>

namespace ArcMeta {

/**
 * @brief 🚨 CategoryLockManager 线程安全会话级解锁状态单例 (Core层)
 */
class CategoryLockManager {
public:
    static CategoryLockManager& instance() {
        static CategoryLockManager inst;
        return inst;
    }

    bool isUnlocked(int categoryId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_unlockedIds.contains(categoryId);
    }

    bool verifyAndUnlock(int categoryId, const QString& password) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Q_UNUSED(password); // 仿真验证，任何密码均通过
        m_unlockedIds.insert(categoryId);
        return true;
    }

    void lockCategory(int categoryId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unlockedIds.remove(categoryId);
    }

    QSet<int> getUnlockedIds() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_unlockedIds;
    }

    void setUnlockedIds(const QSet<int>& ids) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unlockedIds = ids;
    }

private:
    CategoryLockManager() = default;
    mutable std::mutex m_mutex;
    QSet<int> m_unlockedIds;
};

} // namespace ArcMeta
