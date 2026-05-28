#include "NtfsEngine.h"
#include <QString>
#include <QDebug>
#include <winioctl.h>

namespace ArcMeta {

bool NtfsEngine::enablePrivilege(LPCWSTR privilege) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    LUID luid;
    if (!LookupPrivilegeValue(NULL, privilege, &luid)) { CloseHandle(hToken); return false; }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) { CloseHandle(hToken); return false; }
    bool ok = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return ok;
}

int64_t NtfsEngine::filetimeToUnixMs(int64_t filetime) {
    if (filetime <= 116444736000000000LL) return 0;
    return (filetime - 116444736000000000LL) / 10000LL;
}

bool NtfsEngine::loadMftDirect(const std::wstring& volume, DriveResult& result) {
    std::wstring dev = L"\\\\.\\" + volume;
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA_V0 j; DWORD cb;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &j, sizeof(j), &cb, NULL)) {
        CloseHandle(h); return false;
    }
    result.nextUsn = j.NextUsn;
    MFT_ENUM_DATA_V0 ed = {0}; ed.HighUsn = j.NextUsn;
    std::vector<uint8_t> buf(1024 * 1024);
    while (DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &ed, sizeof(ed), buf.data(), (DWORD)buf.size(), &cb, NULL)) {
        if (cb < 8) break;
        uint8_t* p = buf.data() + 8; uint8_t* end = buf.data() + cb;
        while (p < end) {
            USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(p);
            Frn128 frn, parentFrn;
            LARGE_INTEGER timestamp;
            uint32_t attr;
            WORD fileNameLength, fileNameOffset;

            if (header->MajorVersion == 2) {
                USN_RECORD_V2* rec = reinterpret_cast<USN_RECORD_V2*>(p);
                frn = Frn128(rec->FileReferenceNumber);
                parentFrn = Frn128(rec->ParentFileReferenceNumber);
                timestamp = rec->TimeStamp;
                attr = rec->FileAttributes;
                fileNameLength = rec->FileNameLength;
                fileNameOffset = rec->FileNameOffset;
            } else if (header->MajorVersion == 3) {
                USN_RECORD_V3* rec = reinterpret_cast<USN_RECORD_V3*>(p);
                // 2026-06-xx 按照审计要求：直接读取 16 字节 GUID，不再进行 64 位截断
                memcpy(&frn, &rec->FileReferenceNumber, 16);
                memcpy(&parentFrn, &rec->ParentFileReferenceNumber, 16);
                timestamp = rec->TimeStamp;
                attr = rec->FileAttributes;
                fileNameLength = rec->FileNameLength;
                fileNameOffset = rec->FileNameOffset;
            } else {
                p += header->RecordLength; continue;
            }

            RawEntry e;
            e.frn = frn;
            e.parentFrn = parentFrn;
            e.size = 0;
            e.attributes = attr;
            e.modifyTime = filetimeToUnixMs(timestamp.QuadPart);

            QString n = QString::fromUtf16(reinterpret_cast<const char16_t*>(p + fileNameOffset), fileNameLength / 2);
            e.nameUtf8 = n.toUtf8().toStdString();
            result.entries.push_back(std::move(e));
            p += header->RecordLength;
        }
        ed.StartFileReferenceNumber = *reinterpret_cast<DWORDLONG*>(buf.data());
    }
    CloseHandle(h);
    return !result.entries.empty();
}

NtfsEngine::Metadata NtfsEngine::getFileMetadata(const std::wstring& volume, Frn128 frn, const std::wstring& fullPath) {
    Metadata meta = {0, 0, 0, false};

    if (!fullPath.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA attrData;
        if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &attrData)) {
            meta.size = (static_cast<uint64_t>(attrData.nFileSizeHigh) << 32) | attrData.nFileSizeLow;
            meta.attributes = attrData.dwFileAttributes;
            meta.modifyTime = filetimeToUnixMs((static_cast<int64_t>(attrData.ftLastWriteTime.dwHighDateTime) << 32) | attrData.ftLastWriteTime.dwLowDateTime);
            meta.success = true;
            return meta;
        }
    }

    std::wstring rootPath = volume + L"\\";
    HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hHint != INVALID_HANDLE_VALUE) {
        FILE_ID_DESCRIPTOR id = { sizeof(FILE_ID_DESCRIPTOR) };
        if (frn.high == 0) {
            id.Type = FileIdType;
            id.FileId.QuadPart = frn.low;
        } else {
            id.Type = ExtendedFileIdType;
            memcpy(&id.ExtendedFileId, &frn, 16);
        }

        HANDLE hFile = OpenFileById(hHint, &id, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
        if (hFile != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION bhfi;
            if (GetFileInformationByHandle(hFile, &bhfi)) {
                meta.size = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                meta.attributes = bhfi.dwFileAttributes;
                meta.modifyTime = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                meta.success = true;
            }
            CloseHandle(hFile);
        }
        CloseHandle(hHint);
    }
    return meta;
}

} // namespace ArcMeta
