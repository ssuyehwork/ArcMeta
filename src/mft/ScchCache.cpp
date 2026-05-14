#include "ScchCache.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <iostream>
#include <array>
#include <algorithm>

namespace ArcMeta {

const char* scchResultString(ScchResult r) {
    switch (r) {
        case ScchResult::Ok:               return "Ok";
        case ScchResult::FileNotFound:     return "文件不存在";
        case ScchResult::BadMagic:         return "魔数不匹配（非 .scch 文件）";
        case ScchResult::VersionMismatch:  return "版本不兼容，需要重新扫描";
        case ScchResult::CrcMismatch:      return "CRC 校验失败，文件已损坏";
        case ScchResult::Truncated:        return "文件不完整（意外截断）";
        case ScchResult::IoError:          return "I/O 读写错误";
    }
    return "未知错误";
}

// ── CRC32（标准多项式 0xEDB88320）────────────────────────────────

static const std::array<uint32_t, 256> CRC32_TABLE = []() {
    std::array<uint32_t, 256> table;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    return table;
}();

uint32_t ScchCache::computeCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = CRC32_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ── 保存 ─────────────────────────────────────────────────────────

bool ScchCache::save(
    const char*                                  path,
    const std::vector<uint64_t>&                 frns,
    const std::vector<uint64_t>&                 parent_frns,
    const std::vector<int64_t>&                  sizes,
    const std::vector<int64_t>&                  timestamps,
    const std::vector<uint32_t>&                 name_offsets,
    const std::vector<uint32_t>&                 attributes,
    const std::vector<uint8_t>&                  string_pool,
    const std::unordered_map<std::string, uint64_t>& usn_map
) {
    try {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path()
        );

        // ── 1. 先把正文全部序列化到内存缓冲区，便于计算 CRC ──────
        std::vector<uint8_t> body;
        body.reserve(
            frns.size() * (8 + 8 + 8 + 8 + 4 + 4) +
            string_pool.size() + usn_map.size() * sizeof(ScchUsnEntry) + 256
        );

        auto appendRaw = [&](const void* data, size_t len) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
            body.insert(body.end(), p, p + len);
        };

        auto appendU64 = [&](uint64_t v) { appendRaw(&v, 8); };

        // SoA 数组，每组先写 count 再写数据
        auto appendVec64u = [&](const std::vector<uint64_t>& v) {
            appendU64(v.size());
            appendRaw(v.data(), v.size() * 8);
        };
        auto appendVec64i = [&](const std::vector<int64_t>& v) {
            appendU64(v.size());
            appendRaw(v.data(), v.size() * 8);
        };
        auto appendVec32 = [&](const std::vector<uint32_t>& v) {
            appendU64(v.size());
            appendRaw(v.data(), v.size() * 4);
        };

        appendVec64u(frns);
        appendVec64u(parent_frns);
        appendVec64i(sizes);
        appendVec64i(timestamps);
        appendVec32(name_offsets);
        appendVec32(attributes);

        // 字符串池
        appendU64(string_pool.size());
        appendRaw(string_pool.data(), string_pool.size());

        // USN 水位线 map
        appendU64(usn_map.size());
        for (const auto& [drive, usn] : usn_map) {
            ScchUsnEntry entry{};
            size_t copyLen = std::min(drive.size(), sizeof(entry.drive) - 1);
            memcpy(entry.drive, drive.data(), copyLen);
            entry.next_usn = usn;
            appendRaw(&entry, sizeof(entry));
        }

        // ── 2. 计算正文 CRC32 ────────────────────────────────────
        uint32_t crc = computeCrc32(body.data(), body.size());

        // ── 3. 构造头部 ──────────────────────────────────────────
        ScchHeader header{};
        memcpy(header.magic, SCCH_MAGIC, 4);
        header.version_major = SCCH_VERSION_MAJOR;
        header.version_minor = SCCH_VERSION_MINOR;
        header.created_at    = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        header.record_count  = frns.size();
        header.pool_size     = string_pool.size();
        header.usn_map_count = usn_map.size();
        header.crc32         = crc;
        header.flags         = 0;

        // ── 4. 原子写入：先写临时文件，成功后重命名 ─────────────
        std::string tmpPath = std::string(path) + ".tmp";
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) return false;

        f.write(reinterpret_cast<const char*>(&header), sizeof(header));
        f.write(reinterpret_cast<const char*>(body.data()), body.size());
        f.close();

        if (!f) {
            std::filesystem::remove(tmpPath);
            return false;
        }

        // 原子替换
        std::filesystem::rename(tmpPath, path);
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[ScchCache] save failed: " << e.what() << "\n";
        return false;
    }
}

// ── 加载 ─────────────────────────────────────────────────────────

ScchResult ScchCache::load(
    const char*                                  path,
    std::vector<uint64_t>&                       frns,
    std::vector<uint64_t>&                       parent_frns,
    std::vector<int64_t>&                        sizes,
    std::vector<int64_t>&                        timestamps,
    std::vector<uint32_t>&                       name_offsets,
    std::vector<uint32_t>&                       attributes,
    std::vector<uint8_t>&                        string_pool,
    std::unordered_map<std::string, uint64_t>&   usn_map
) {
    try {
        if (!std::filesystem::exists(path))
            return ScchResult::FileNotFound;

        std::ifstream f(path, std::ios::binary);
        if (!f) return ScchResult::IoError;

        // ── 1. 读头部 ────────────────────────────────────────────
        ScchHeader header{};
        f.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!f) return ScchResult::Truncated;

        // ── 2. 验证魔数 ──────────────────────────────────────────
        if (memcmp(header.magic, SCCH_MAGIC, 4) != 0)
            return ScchResult::BadMagic;

        // ── 3. 验证版本（主版本必须一致）────────────────────────
        if (header.version_major != SCCH_VERSION_MAJOR)
            return ScchResult::VersionMismatch;

        // ── 4. 读取全部正文到内存 ────────────────────────────────
        f.seekg(0, std::ios::end);
        size_t fileSize = f.tellg();
        if (fileSize < sizeof(ScchHeader)) return ScchResult::Truncated;
        size_t bodySize = fileSize - sizeof(ScchHeader);
        f.seekg(sizeof(ScchHeader), std::ios::beg);
        
        std::vector<uint8_t> body(bodySize);
        f.read(reinterpret_cast<char*>(body.data()), bodySize);
        if (!f) return ScchResult::Truncated;

        // ── 5. 验证 CRC32 ────────────────────────────────────────
        uint32_t actualCrc = computeCrc32(body.data(), body.size());
        if (actualCrc != header.crc32)
            return ScchResult::CrcMismatch;

        // ── 6. 从 body 反序列化 SoA ──────────────────────────────
        const uint8_t* p   = body.data();
        const uint8_t* end = body.data() + body.size();

        auto readU64 = [&](uint64_t& v) -> bool {
            if (p + 8 > end) return false;
            memcpy(&v, p, 8); p += 8; return true;
        };

        auto readVec64u = [&](std::vector<uint64_t>& v, uint64_t expected) -> bool {
            uint64_t count = 0;
            if (!readU64(count)) return false;
            if (count != expected) return false;
            if (p + count * 8 > end) return false;
            v.resize(count);
            memcpy(v.data(), p, count * 8);
            p += count * 8;
            return true;
        };

        auto readVec64i = [&](std::vector<int64_t>& v, uint64_t expected) -> bool {
            uint64_t count = 0;
            if (!readU64(count)) return false;
            if (count != expected) return false;
            if (p + count * 8 > end) return false;
            v.resize(count);
            memcpy(v.data(), p, count * 8);
            p += count * 8;
            return true;
        };

        auto readVec32 = [&](std::vector<uint32_t>& v, uint64_t expected) -> bool {
            uint64_t count = 0;
            if (!readU64(count)) return false;
            if (count != expected) return false;
            if (p + count * 4 > end) return false;
            v.resize(count);
            memcpy(v.data(), p, count * 4);
            p += count * 4;
            return true;
        };

        uint64_t rc = header.record_count;
        if (!readVec64u(frns,         rc)) return ScchResult::Truncated;
        if (!readVec64u(parent_frns,  rc)) return ScchResult::Truncated;
        if (!readVec64i(sizes,        rc)) return ScchResult::Truncated;
        if (!readVec64i(timestamps,   rc)) return ScchResult::Truncated;
        if (!readVec32(name_offsets, rc)) return ScchResult::Truncated;
        if (!readVec32(attributes,   rc)) return ScchResult::Truncated;

        // 字符串池
        uint64_t poolSize = 0;
        if (!readU64(poolSize)) return ScchResult::Truncated;
        if (poolSize != header.pool_size) return ScchResult::Truncated;
        if (p + poolSize > end) return ScchResult::Truncated;
        string_pool.assign(p, p + poolSize);
        p += poolSize;

        // USN 水位线
        uint64_t usnCount = 0;
        if (!readU64(usnCount)) return ScchResult::Truncated;
        if (usnCount != header.usn_map_count) return ScchResult::Truncated;
        usn_map.clear();
        for (uint64_t i = 0; i < usnCount; ++i) {
            if (p + sizeof(ScchUsnEntry) > end) return ScchResult::Truncated;
            ScchUsnEntry entry{};
            memcpy(&entry, p, sizeof(entry));
            p += sizeof(entry);
            usn_map[std::string(entry.drive)] = entry.next_usn;
        }

        return ScchResult::Ok;

    } catch (const std::exception& e) {
        std::cerr << "[ScchCache] load failed: " << e.what() << "\n";
        return ScchResult::IoError;
    }
}

} // namespace ArcMeta
