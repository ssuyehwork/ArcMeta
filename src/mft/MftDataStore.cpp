#include "MftDataStore.h"
#include <algorithm>
#include <numeric>
#include <execution>

namespace ArcMeta {

MftDataStore::MftDataStore() {
    clear();
}

void MftDataStore::clear() {
    m_frns.clear();
    m_drive_indices.clear();
    m_parent_frns.clear();
    m_sizes.clear();
    m_timestamps.clear();
    m_name_offsets.clear();
    m_attributes.clear();
    m_metadata_fetched.clear();
    m_string_pool.clear();
    m_key_to_idx.clear();
    m_sorted_indices.clear();
    m_dead_count = 0;
    m_wasted_string_bytes = 0;
}

std::shared_ptr<MftDataStore> MftDataStore::compact() const {
    auto newData = std::make_shared<MftDataStore>();
    size_t count = m_frns.size();
    size_t activeCount = (count > m_dead_count) ? (count - m_dead_count) : 0;
    newData->m_frns.reserve(activeCount);
    newData->m_drive_indices.reserve(activeCount);
    newData->m_parent_frns.reserve(activeCount);
    newData->m_sizes.reserve(activeCount);
    newData->m_timestamps.reserve(activeCount);
    newData->m_name_offsets.reserve(activeCount);
    newData->m_attributes.reserve(activeCount);
    newData->m_metadata_fetched.reserve(activeCount);
    newData->m_string_pool.reserve(m_string_pool.size() - m_wasted_string_bytes);

    for (size_t i = 0; i < count; ++i) {
        if (m_frns[i].isZero()) continue;
        uint32_t newIdx = (uint32_t)newData->m_frns.size();
        newData->m_key_to_idx[{m_drive_indices[i], m_frns[i]}] = newIdx;
        newData->m_frns.push_back(m_frns[i]);
        newData->m_drive_indices.push_back(m_drive_indices[i]);
        newData->m_parent_frns.push_back(m_parent_frns[i]);
        newData->m_sizes.push_back(m_sizes[i]);
        newData->m_timestamps.push_back(m_timestamps[i]);
        newData->m_attributes.push_back(m_attributes[i]);
        newData->m_metadata_fetched.push_back(m_metadata_fetched[i]);
        const char* name = getNamePtr((uint32_t)i);
        newData->m_name_offsets.push_back(newData->addString(name ? name : ""));
    }

    newData->m_sorted_indices.resize(newData->m_frns.size());
    std::iota(newData->m_sorted_indices.begin(), newData->m_sorted_indices.end(), 0);
    std::sort((std::execution::par), newData->m_sorted_indices.begin(), newData->m_sorted_indices.end(), [&newData](uint32_t a, uint32_t b) {
        const char* s1 = newData->getNamePtr(a);
        const char* s2 = newData->getNamePtr(b);
        return _stricmp(s1 ? s1 : "", s2 ? s2 : "") < 0;
    });
    return newData;
}

} // namespace ArcMeta
