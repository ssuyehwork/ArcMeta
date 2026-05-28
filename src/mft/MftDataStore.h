#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <memory>
#include <QString>
#include <QReadWriteLock>

namespace ArcMeta {

/**
 * @brief 128位文件引用号 (支持 USN V3 / ReFS)
 * 杜绝 64 位截断，支持 GUID 级别唯一性。
 */
struct Frn128 {
    uint64_t low = 0;
    uint64_t high = 0;
    Frn128() = default;
    Frn128(uint64_t l, uint64_t h = 0) : low(l), high(h) {}
    bool isZero() const { return low == 0 && high == 0; }
    bool operator==(const Frn128& o) const { return low == o.low && high == o.high; }
    bool operator!=(const Frn128& o) const { return !(*this == o); }
    bool operator<(const Frn128& o) const { return high < o.high || (high == o.high && low < o.low); }
};

/**
 * @brief 复合主键：驱动器索引 + 128位 FRN
 */
struct FullKey {
    uint32_t driveIdx;
    Frn128   frn;
    bool operator==(const FullKey& o) const { return driveIdx == o.driveIdx && frn == o.frn; }
    bool operator<(const FullKey& o) const { return driveIdx < o.driveIdx || (driveIdx == o.driveIdx && frn < o.frn); }
};

struct FullKeyHash {
    size_t operator()(const FullKey& k) const {
        size_t h1 = std::hash<uint32_t>{}(k.driveIdx);
        size_t h2 = std::hash<uint64_t>{}(k.frn.low);
        size_t h3 = std::hash<uint64_t>{}(k.frn.high);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)) ^ (h3 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2));
    }
};

/**
 * @brief 数据仓库层 (DataStore)：SoA 结构物理存储
 */
class MftDataStore {
public:
    MftDataStore();
    ~MftDataStore() = default;
    void clear();

    std::vector<Frn128>   m_frns;
    std::vector<uint32_t> m_drive_indices;
    std::vector<Frn128>   m_parent_frns;
    std::vector<int64_t>  m_sizes;
    std::vector<int64_t>  m_timestamps;
    std::vector<uint32_t> m_name_offsets;
    std::vector<uint32_t> m_attributes;
    std::vector<uint8_t>  m_metadata_fetched;
    std::vector<uint8_t>  m_string_pool;

    std::unordered_map<FullKey, uint32_t, FullKeyHash> m_key_to_idx;
    std::vector<uint32_t> m_sorted_indices;

    size_t m_dead_count = 0;
    size_t m_wasted_string_bytes = 0;

    std::shared_ptr<MftDataStore> compact() const;

    inline const char* getNamePtr(uint32_t index) const {
        if (index >= m_name_offsets.size()) return nullptr;
        return reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[index]);
    }

    uint32_t addString(const std::string& str) {
        uint32_t offset = (uint32_t)m_string_pool.size();
        m_string_pool.insert(m_string_pool.end(), str.begin(), str.end());
        m_string_pool.push_back('\0');
        return offset;
    }

    void updateString(uint32_t entryIdx, const std::string& newStr) {
        uint32_t oldOff = m_name_offsets[entryIdx];
        const char* oldPtr = getNamePtr(entryIdx);
        size_t oldLen = oldPtr ? strlen(oldPtr) : 0;
        if (newStr.size() <= oldLen) {
            memcpy(m_string_pool.data() + oldOff, newStr.c_str(), newStr.size());
            m_string_pool[oldOff + newStr.size()] = '\0';
            if (newStr.size() < oldLen) m_wasted_string_bytes += (oldLen - newStr.size());
        } else {
            m_wasted_string_bytes += (oldLen + 1);
            m_name_offsets[entryIdx] = addString(newStr);
        }
    }
};

} // namespace ArcMeta
