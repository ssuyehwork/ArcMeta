#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <winioctl.h>
#include <cstdint>
#include "MftDataStore.h"

namespace ArcMeta {

struct RawEntry {
    Frn128 frn;
    Frn128 parentFrn;
    uint64_t size;
    uint32_t attributes;
    int64_t  modifyTime;
    std::string nameUtf8;
};

struct DriveResult {
    std::vector<RawEntry> entries;
    uint64_t nextUsn;
};

/**
 * @brief 物理引擎层：封装所有 WinAPI 调用，提供纯净的 MFT 扫描和属性获取接口
 */
class NtfsEngine {
public:
    static bool enablePrivilege(LPCWSTR privilege);
    static int64_t filetimeToUnixMs(int64_t filetime);

    static bool loadMftDirect(const std::wstring& volume, DriveResult& result);

    struct Metadata {
        uint64_t size;
        uint32_t attributes;
        int64_t  modifyTime;
        bool success;
    };
    static Metadata getFileMetadata(const std::wstring& volume, Frn128 frn, const std::wstring& fullPath = L"");
};

} // namespace ArcMeta
