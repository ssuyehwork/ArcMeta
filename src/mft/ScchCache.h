#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArcMeta {

// 当前格式版本
constexpr uint16_t SCCH_VERSION_MAJOR = 1;
constexpr uint16_t SCCH_VERSION_MINOR = 0;
constexpr char     SCCH_MAGIC[4]      = {'S','C','C','H'};
constexpr char     SCCH_DEFAULT_PATH[] = "ArcMeta/cache/index.scch";

#pragma pack(push, 1)
struct ScchHeader {
    char     magic[4];           // "SCCH"
    uint16_t version_major;
    uint16_t version_minor;
    int64_t  created_at;         // Unix 毫秒
    uint64_t record_count;
    uint64_t pool_size;
    uint64_t usn_map_count;
    uint32_t crc32;              // 头部之后所有字节的 CRC32
    uint32_t flags;              // 保留，写 0
    // 总计 48 字节，对齐友好
};
#pragma pack(pop)

static_assert(sizeof(ScchHeader) == 48, "ScchHeader size mismatch");

struct ScchUsnEntry {
    char     drive[4];           // e.g. "C:\0"
    uint64_t next_usn;
};

// 读写结果
enum class ScchResult {
    Ok,
    FileNotFound,
    BadMagic,
    VersionMismatch,
    CrcMismatch,
    Truncated,
    IoError,
};

const char* scchResultString(ScchResult r);

class ScchCache {
public:
    // 写入
    static bool save(
        const char*                                  path,
        const std::vector<uint64_t>&                 frns,
        const std::vector<uint64_t>&                 parent_frns,
        const std::vector<int64_t>&                  sizes,
        const std::vector<int64_t>&                  timestamps,
        const std::vector<uint32_t>&                 name_offsets,
        const std::vector<uint32_t>&                 attributes,
        const std::vector<uint8_t>&                  string_pool,
        const std::unordered_map<std::string, uint64_t>& usn_map
    );

    // 读取
    static ScchResult load(
        const char*                                  path,
        std::vector<uint64_t>&                       frns,
        std::vector<uint64_t>&                       parent_frns,
        std::vector<int64_t>&                        sizes,
        std::vector<int64_t>&                        timestamps,
        std::vector<uint32_t>&                       name_offsets,
        std::vector<uint32_t>&                       attributes,
        std::vector<uint8_t>&                        string_pool,
        std::unordered_map<std::string, uint64_t>&   usn_map
    );

private:
    static uint32_t computeCrc32(const uint8_t* data, size_t len);
};

} // namespace ArcMeta
