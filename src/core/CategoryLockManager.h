#pragma once

#include <QSet>
#include <QString>
#include <QCryptographicHash>
#include <mutex>
#include "../meta/CategoryRepo.h"

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
        Category cat = CategoryRepo::getById(categoryId);
        if (!cat.encrypted) {
            m_unlockedIds.insert(categoryId);
            return true;
        }

        QString storedData = QString::fromStdWString(cat.encryptHint);

        // 防死锁兼容：若不包含 :::，说明是旧版遗留的未哈希数据或老格式
        if (!storedData.contains(":::")) {
            m_unlockedIds.insert(categoryId);
            return true;
        }

        // 解析出真密文哈希
        QString realHash = storedData.section(":::", 0, 0);

        // 计算输入密码的 SHA-256 哈希
        QString inputHash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();

        if (!realHash.isEmpty() && inputHash == realHash) {
            m_unlockedIds.insert(categoryId);
            return true;
        }

        return false;
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
