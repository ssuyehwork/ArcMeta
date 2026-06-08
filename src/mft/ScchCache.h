#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ArcMeta {

constexpr char SCCH_MAGIC[4] = {'S','C','C','H'};
constexpr uint16_t SCCH_VERSION = 2; // 升级版本号

#pragma pack(push, 1)
struct ScchAnchor {
    char     magic[4];           // "SCCH"
    uint16_t version;
    char     vol_serial[16];     // 存储该磁盘的卷序列号 e.g. "ABCD1234\0"
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
