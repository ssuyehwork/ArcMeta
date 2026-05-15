#pragma once
#include <list>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace ArcMeta {

template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : m_capacity(capacity) {}

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            m_list.erase(it->second);
            m_map.erase(it);
        } else if (m_map.size() >= m_capacity) {
            auto last = m_list.back();
            m_map.erase(last.first);
            m_list.pop_back();
        }
        m_list.push_front({key, value});
        m_map[key] = m_list.begin();
    }

    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(key);
        if (it == m_map.end()) return std::nullopt;

        m_list.splice(m_list.begin(), m_list, it->second);
        return it->second->second;
    }

    void remove(const K& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            m_list.erase(it->second);
            m_map.erase(it);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map.clear();
        m_list.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_map.size();
    }

private:
    size_t m_capacity;
    std::list<std::pair<K, V>> m_list;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> m_map;
    mutable std::mutex m_mutex;
};

} // namespace ArcMeta
