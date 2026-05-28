#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ScchCache.h"
#include <windows.h>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <iostream>
#include <array>
#include <algorithm>

namespace ArcMeta {

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

bool ScchCache::save(
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
) {
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path());
    std::string tmpPath = p.string() + ".tmp";
    std::wstring wTmpPath = std::filesystem::path(tmpPath).wstring();

    size_t bodySize = 0;
    bodySize += 8 + frns.size() * 16;
    bodySize += 8 + drive_indices.size() * 4;
    bodySize += 8 + parent_frns.size() * 16;
    bodySize += 8 + sizes.size() * 8;
    bodySize += 8 + timestamps.size() * 8;
    bodySize += 8 + name_offsets.size() * 4;
    bodySize += 8 + attributes.size() * 4;
    bodySize += 8 + metadata_fetched.size();
    bodySize += 8 + string_pool.size();
    bodySize += 8 + sorted_indices.size() * 4;
    bodySize += 8 + usn_map.size() * sizeof(ScchUsnEntry);

    size_t totalSize = sizeof(ScchHeader) + bodySize;

    HANDLE hFile = CreateFileW(wTmpPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li; li.QuadPart = totalSize;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) || !SetEndOfFile(hFile)) { CloseHandle(hFile); return false; }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (!hMap) { CloseHandle(hFile); return false; }

    uint8_t* base = static_cast<uint8_t*>(MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0));
    if (!base) { CloseHandle(hMap); CloseHandle(hFile); return false; }

    uint8_t* ptr = base + sizeof(ScchHeader);
    auto writeRaw = [&](const void* data, size_t len) { memcpy(ptr, data, len); ptr += len; };
    auto writeU64 = [&](uint64_t v) { writeRaw(&v, 8); };

    writeU64(frns.size()); writeRaw(frns.data(), frns.size() * 16);
    writeU64(drive_indices.size()); writeRaw(drive_indices.data(), drive_indices.size() * 4);
    writeU64(parent_frns.size()); writeRaw(parent_frns.data(), parent_frns.size() * 16);
    writeU64(sizes.size()); writeRaw(sizes.data(), sizes.size() * 8);
    writeU64(timestamps.size()); writeRaw(timestamps.data(), timestamps.size() * 8);
    writeU64(name_offsets.size()); writeRaw(name_offsets.data(), name_offsets.size() * 4);
    writeU64(attributes.size()); writeRaw(attributes.data(), attributes.size() * 4);
    writeU64(metadata_fetched.size()); writeRaw(metadata_fetched.data(), metadata_fetched.size());
    writeU64(string_pool.size()); writeRaw(string_pool.data(), string_pool.size());
    writeU64(sorted_indices.size()); writeRaw(sorted_indices.data(), sorted_indices.size() * 4);

    writeU64(usn_map.size());
    for (const auto& [drive, usn] : usn_map) {
        ScchUsnEntry entry{};
        strncpy(entry.drive, drive.c_str(), 3);
        entry.next_usn = usn;
        writeRaw(&entry, sizeof(entry));
    }

    ScchHeader* header = reinterpret_cast<ScchHeader*>(base);
    memcpy(header->magic, SCCH_MAGIC, 4);
    header->version_major = SCCH_VERSION_MAJOR;
    header->version_minor = SCCH_VERSION_MINOR;
    header->created_at = std::chrono::system_clock::now().time_since_epoch().count();
    header->record_count = frns.size();
    header->pool_size = string_pool.size();
    header->usn_map_count = usn_map.size();
    header->sorted_indices_count = sorted_indices.size();
    header->crc32 = computeCrc32(base + sizeof(ScchHeader), bodySize);
    header->flags = 0;

    UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile);
    std::filesystem::rename(tmpPath, path);
    return true;
}

ScchResult ScchCache::load(
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
) {
    std::wstring wpath = std::filesystem::path(path).wstring();
    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return ScchResult::FileNotFound;

    LARGE_INTEGER li; GetFileSizeEx(hFile, &li);
    size_t fileSize = (size_t)li.QuadPart;
    if (fileSize < sizeof(ScchHeader)) { CloseHandle(hFile); return ScchResult::Truncated; }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    const uint8_t* base = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);

    const ScchHeader* header = (const ScchHeader*)base;
    if (memcmp(header->magic, SCCH_MAGIC, 4) != 0) { UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile); return ScchResult::BadMagic; }
    if (header->version_major != SCCH_VERSION_MAJOR) { UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile); return ScchResult::VersionMismatch; }

    const uint8_t* ptr = base + sizeof(ScchHeader);
    auto readU64 = [&]() { uint64_t v; memcpy(&v, ptr, 8); ptr += 8; return v; };

    uint64_t count = readU64(); frns.insert(frns.end(), (Frn128*)ptr, (Frn128*)ptr + count); ptr += count * 16;
    count = readU64(); drive_indices.insert(drive_indices.end(), (uint32_t*)ptr, (uint32_t*)ptr + count); ptr += count * 4;
    count = readU64(); parent_frns.insert(parent_frns.end(), (Frn128*)ptr, (Frn128*)ptr + count); ptr += count * 16;
    count = readU64(); sizes.insert(sizes.end(), (int64_t*)ptr, (int64_t*)ptr + count); ptr += count * 8;
    count = readU64(); timestamps.insert(timestamps.end(), (int64_t*)ptr, (int64_t*)ptr + count); ptr += count * 8;
    count = readU64(); name_offsets.insert(name_offsets.end(), (uint32_t*)ptr, (uint32_t*)ptr + count); ptr += count * 4;
    count = readU64(); attributes.insert(attributes.end(), (uint32_t*)ptr, (uint32_t*)ptr + count); ptr += count * 4;
    count = readU64(); metadata_fetched.insert(metadata_fetched.end(), ptr, ptr + count); ptr += count;
    count = readU64(); string_pool.insert(string_pool.end(), ptr, ptr + count); ptr += count;
    count = readU64(); sorted_indices.insert(sorted_indices.end(), (uint32_t*)ptr, (uint32_t*)ptr + count); ptr += count * 4;

    uint64_t usnCount = readU64();
    for (uint64_t i = 0; i < usnCount; ++i) {
        ScchUsnEntry entry; memcpy(&entry, ptr, sizeof(entry)); ptr += sizeof(entry);
        usn_map[std::string(entry.drive)] = entry.next_usn;
    }

    UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile);
    return ScchResult::Ok;
}

} // namespace ArcMeta
