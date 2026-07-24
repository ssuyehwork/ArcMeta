#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DirectoryBatchEnumerator.h"
#include "../meta/MetadataManager.h"
#include <windows.h>
#include <fileapi.h>
#include <winbase.h>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>

namespace ArcMeta {

bool DirectoryBatchEnumerator::enumerate(const std::wstring& dirPath, std::vector<BatchEnumeratedEntry>& outEntries) {
    // 1. 标准化路径并处理尾斜杠
    std::wstring normalizedDir = MetadataManager::normalizePath(dirPath);
    if (normalizedDir.empty()) return false;

    // 获取卷序列号，以便生成 File ID (128-bit)
    std::wstring vol = MetadataManager::getVolumeSerialNumber(normalizedDir);

    bool useBackupPath = false;

    // 2. 尝试使用 Windows GetFileInformationByHandleEx & FileIdBothDirectoryInfo API 一次性获取所有信息
    HANDLE hDir = CreateFileW(
        normalizedDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        qWarning() << "[Enumerator] 无法打开目录句柄，转而执行普通降级枚举:" << QString::fromStdWString(normalizedDir);
        useBackupPath = true;
    } else {
        // 缓存容器
        const DWORD bufferSize = 1024 * 1024; // 1MB 缓冲区，获取尽可能多的子项
        std::vector<BYTE> buffer(bufferSize);

        bool success = true;

        while (true) {
            if (!GetFileInformationByHandleEx(
                    hDir,
                    FileIdBothDirectoryInfo,
                    buffer.data(),
                    bufferSize
                )) {
                DWORD err = GetLastError();
                if (err == ERROR_NO_MORE_FILES) {
                    break; // 顺利枚举完毕
                }
                qWarning() << "[Enumerator] GetFileInformationByHandleEx 错误码:" << err << "将降级普通枚举。";
                success = false;
                break;
            }

            BYTE* pCurrent = buffer.data();
            while (true) {
                FILE_ID_BOTH_DIR_INFO* pInfo = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(pCurrent);

                // 获取文件名
                std::wstring name(pInfo->FileName, pInfo->FileNameLength / sizeof(wchar_t));

                // 过滤 '.' 与 '..'
                if (name != L"." && name != L"..") {
                    BatchEnumeratedEntry entry;
                    entry.name = name;
                    entry.fullPath = normalizedDir + (normalizedDir.back() == L'\\' || normalizedDir.back() == L'/' ? L"" : L"\\") + name;
                    entry.fullPath = MetadataManager::normalizePath(entry.fullPath);
                    entry.isDir = (pInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    entry.fileSize = pInfo->EndOfFile.QuadPart;

                    // 时间戳转换 (FILETIME -> MS)
                    auto toMS = [](const LARGE_INTEGER& li) {
                        ULARGE_INTEGER ull;
                        ull.LowPart = li.LowPart;
                        ull.HighPart = li.HighPart;
                        return static_cast<long long>((ull.QuadPart - 116444736000000000ULL) / 10000ULL);
                    };

                    entry.ctime = toMS(pInfo->CreationTime);
                    entry.mtime = toMS(pInfo->LastWriteTime);
                    entry.atime = toMS(pInfo->LastAccessTime);

                    // FRN & File ID
                    wchar_t frnBuf[17];
                    unsigned long long fullFrn = pInfo->FileId.QuadPart;
                    swprintf(frnBuf, 17, L"%016llX", fullFrn);
                    entry.frn = frnBuf;
                    entry.fileId128 = MetadataManager::generateFallbackFid(vol, frnBuf);

                    outEntries.push_back(std::move(entry));
                }

                if (pInfo->NextEntryOffset == 0) {
                    break;
                }
                pCurrent += pInfo->NextEntryOffset;
            }
        }

        CloseHandle(hDir);

        if (!success) {
            useBackupPath = true;
        }
    }

    if (!useBackupPath) {
        return true;
    }

    // 3. 降级路径：使用 QDirIterator + fetchWinApiMetadataDirect 进行稳健解析并记录日志
    outEntries.clear();
    qInfo() << "[Enumerator] 开始执行降级枚举机制 ->" << QString::fromStdWString(normalizedDir);

    QDirIterator it(QString::fromStdWString(normalizedDir), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        std::wstring itemPath = MetadataManager::normalizePath(fi.absoluteFilePath().toStdWString());

        BatchEnumeratedEntry entry;
        entry.name = fi.fileName().toStdWString();
        entry.fullPath = itemPath;
        entry.isDir = fi.isDir();

        // 调用 existing metadata 提取
        std::wstring outFrn;
        long long outSize = 0, outCtime = 0, outMtime = 0, outAtime = 0;
        if (MetadataManager::fetchWinApiMetadataDirect(
                itemPath, entry.fileId128, &outFrn, &outSize, nullptr, &outCtime, &outMtime, &outAtime)) {
            entry.frn = outFrn;
            entry.fileSize = outSize;
            entry.ctime = outCtime;
            entry.mtime = outMtime;
            entry.atime = outAtime;
        } else {
            // fallback
            entry.fileId128 = MetadataManager::generateDeterministicSha256Id(itemPath);
            entry.frn = MetadataManager::generateDeterministicFrn(itemPath);
            entry.fileSize = fi.size();
            entry.ctime = fi.birthTime().toMSecsSinceEpoch();
            entry.mtime = fi.lastModified().toMSecsSinceEpoch();
            entry.atime = fi.lastRead().toMSecsSinceEpoch();
        }

        outEntries.push_back(std::move(entry));
    }

    return true;
}

} // namespace ArcMeta
