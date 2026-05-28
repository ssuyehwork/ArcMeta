#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "MftDataStore.h"

namespace ArcMeta {

// 当前格式版本 2.0 (支持 128位 FRN)
constexpr uint16_t SCCH_VERSION_MAJOR = 2;
constexpr uint16_t SCCH_VERSION_MINOR = 0;
constexpr char     SCCH_MAGIC[4]      = {'S','C','C','H'};

#pragma pack(push, 1)
struct ScchHeader {
    char     magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    int64_t  created_at;
    uint64_t record_count;
    uint64_t pool_size;
    uint64_t usn_map_count;
    uint64_t sorted_indices_count;
    uint32_t crc32;
    uint32_t flags;
};
#pragma pack(pop)

struct ScchUsnEntry {
    char     drive[4];
    uint64_t next_usn;
};

enum class ScchResult {
    Ok,
    FileNotFound,
    BadMagic,
    VersionMismatch,
    CrcMismatch,
    Truncated,
    IoError,
};

class ScchCache {
public:
    static bool save(
        const char*                                  path,
        const std::vector<Frn128>&                   frns,
        const std::vector<uint32_t>&                 drive_indices,
        const std::vector<Frn128>&                   parent_frns,
        const std::vector<int64_t>&                  sizes,
        const std::vector<int64_t>&                  timestamps,
        const std::vector<uint32_t>&                 name_offsets,
        const std::vector<uint32_t>&                 attributes,
        const std::vector<uint8_t>&                  metadata_fetched,
        const std::vector<uint8_t>&                  string_pool,
        const std::vector<uint32_t>&                 sorted_indices,
        const std::unordered_map<std::string, uint64_t>& usn_map
    );

    static ScchResult load(
        const char*                                  path,
        std::vector<Frn128>&                         frns,
        std::vector<uint32_t>&                       drive_indices,
        std::vector<Frn128>&                         parent_frns,
        std::vector<int64_t>&                        sizes,
        std::vector<int64_t>&                        timestamps,
        std::vector<uint32_t>&                       name_offsets,
        std::vector<uint32_t>&                       attributes,
        std::vector<uint8_t>&                        metadata_fetched,
        std::vector<uint8_t>&                        string_pool,
        std::vector<uint32_t>&                       sorted_indices,
        std::unordered_map<std::string, uint64_t>&   usn_map
    );

private:
    static uint32_t computeCrc32(const uint8_t* data, size_t len);
};

} // namespace ArcMeta
