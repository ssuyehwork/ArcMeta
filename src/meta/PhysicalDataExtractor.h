#ifndef QuarkMeta_PHYSICAL_DATA_EXTRACTOR_H
#define QuarkMeta_PHYSICAL_DATA_EXTRACTOR_H

#include <string>
#include <QString>
#include <windows.h>

namespace QuarkMeta {

/**
 * @brief 专职负责 Windows 物理磁盘 I/O 元数据与 FRN 指纹直接获取的静态纯函数服务
 */
class PhysicalDataExtractor {
public:
    /**
     * @brief 通过 WinAPI 获取 File ID 和基础元数据 (从 MetadataManager 物理移入)
     */
    static bool fetchWinApiMetadataDirect(
        const std::wstring& path, 
        std::string& outId128, 
        std::wstring* outFrn = nullptr, 
        long long* outSize = nullptr, 
        std::wstring* outType = nullptr, 
        long long* outCtime = nullptr, 
        long long* outMtime = nullptr, 
        long long* outAtime = nullptr
    );
};

} // namespace QuarkMeta

#endif // QuarkMeta_PHYSICAL_DATA_EXTRACTOR_H
