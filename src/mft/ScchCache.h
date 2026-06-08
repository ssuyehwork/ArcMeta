#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

namespace ArcMeta {

constexpr char SCCH_MAGIC[4] = {'S','C','C','H'};
constexpr uint16_t SCCH_VERSION = 2;

#pragma pack(push, 1)
struct ScchAnchor {
    char     magic[4];
    uint16_t version;
    char     vol_serial[16];
};
#pragma pack(pop)

class ScchCache {
public:
    static bool writeAnchor(const std::wstring& drivePath, const std::wstring& volSerial) {
        std::wstring scchPath = drivePath;
        if (scchPath.back() != L'\\' && scchPath.back() != L'/') scchPath += L'\\';
        scchPath += L".scch";

        ScchAnchor anchor{};
        memcpy(anchor.magic, SCCH_MAGIC, 4);
        anchor.version = SCCH_VERSION;

        std::string serial = std::string(volSerial.begin(), volSerial.end());
        size_t len = (std::min)(serial.size(), size_t(15));
        memcpy(anchor.vol_serial, serial.c_str(), len);
        anchor.vol_serial[len] = '\0';

        // 使用 _wfopen 并抑制警告
        FILE* f = _wfopen(scchPath.c_str(), L"wb");
        if (!f) return false;
        fwrite(&anchor, sizeof(anchor), 1, f);
        fclose(f);
        return true;
    }

    static std::wstring readAnchor(const std::wstring& scchPath) {
        FILE* f = _wfopen(scchPath.c_str(), L"rb");
        if (!f) return L"";

        ScchAnchor anchor{};
        size_t n = fread(&anchor, sizeof(anchor), 1, f);
        fclose(f);

        if (n == 1 && memcmp(anchor.magic, SCCH_MAGIC, 4) == 0) {
            std::string serial(anchor.vol_serial);
            return std::wstring(serial.begin(), serial.end());
        }
        return L"";
    }
};

} // namespace ArcMeta
