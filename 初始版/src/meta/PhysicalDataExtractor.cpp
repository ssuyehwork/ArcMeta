#include "PhysicalDataExtractor.h"
#include <QFileInfo>
#include <QDebug>

namespace ArcMeta {

bool PhysicalDataExtractor::fetchWinApiMetadataDirect(
    const std::wstring& path, 
    std::string& outId128, 
    std::wstring* outFrn, 
    long long* outSize, 
    std::wstring* outType, 
    long long* outCtime, 
    long long* outMtime, 
    long long* outAtime
) {
    // 为 Windows 平台原生 API 建立安全的兼容封装，返回由 FRN 与序列号组成的 128 位唯一物理指纹。
    HANDLE hFile = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    BY_HANDLE_FILE_INFORMATION fileInfo;
    if (!GetFileInformationByHandle(hFile, &fileInfo)) {
        CloseHandle(hFile);
        return false;
    }

    // 组合生成 128 位物理 File ID
    DWORD volSerial = fileInfo.dwVolumeSerialNumber;
    DWORD indexHigh = fileInfo.nFileIndexHigh;
    DWORD indexLow = fileInfo.nFileIndexLow;

    char buf[64];
    sprintf_s(buf, "%08X-%08X%08X", volSerial, indexHigh, indexLow);
    outId128 = buf;

    if (outFrn) {
        // FRN 序列化
        wchar_t frnBuf[64];
        swprintf_s(frnBuf, L"%08X%08X", indexHigh, indexLow);
        *outFrn = frnBuf;
    }

    if (outSize) {
        *outSize = ((long long)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
    }

    if (outType) {
        *outType = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"folder" : L"file";
    }

    if (outCtime) {
        ULARGE_INTEGER li;
        li.LowPart = fileInfo.ftCreationTime.dwLowDateTime;
        li.HighPart = fileInfo.ftCreationTime.dwHighDateTime;
        *outCtime = li.QuadPart;
    }

    if (outMtime) {
        ULARGE_INTEGER li;
        li.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
        li.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
        *outMtime = li.QuadPart;
    }

    if (outAtime) {
        ULARGE_INTEGER li;
        li.LowPart = fileInfo.ftLastAccessTime.dwLowDateTime;
        li.HighPart = fileInfo.ftLastAccessTime.dwHighDateTime;
        *outAtime = li.QuadPart;
    }

    CloseHandle(hFile);
    return true;
}

} // namespace ArcMeta
