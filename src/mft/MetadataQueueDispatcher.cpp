#include "MetadataQueueDispatcher.h"
#include "MftReader.h"
#include <QtConcurrent/QtConcurrent>
#include <windows.h>

namespace ArcMeta {

static int64_t filetimeToUnixMs(int64_t filetime) {
    if (filetime <= 116444736000000000LL) return 0;
    return (filetime - 116444736000000000LL) / 10000LL;
}

MetadataQueueDispatcher& MetadataQueueDispatcher::instance() {
    static MetadataQueueDispatcher inst;
    return inst;
}

MetadataQueueDispatcher::MetadataQueueDispatcher(QObject* parent) : QObject(parent) {}

void MetadataQueueDispatcher::requestMetadata(int index, uint64_t frn, const std::wstring& volume) {
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        m_metadata_queue.push_back({index, frn, volume});
    }
    processMetadataQueue();
}

void MetadataQueueDispatcher::clear() {
    std::lock_guard<std::mutex> qLock(m_queueMutex);
    m_metadata_queue.clear();
}

void MetadataQueueDispatcher::processMetadataQueue() {
    if (m_active_metadata_tasks.load() >= 4) return;

    MetadataTask task;
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        if (m_metadata_queue.empty()) return;
        task = m_metadata_queue.back();
        m_metadata_queue.pop_back();
    }

    m_active_metadata_tasks.fetch_add(1);
    (void)QtConcurrent::run([this, task]() {
        int index = task.index;
        uint64_t frn = task.frn;
        std::wstring volume = task.volume;

        MftReader& reader = MftReader::instance();
        QString fullPath = reader.getFullPath(index);
        WIN32_FILE_ATTRIBUTE_DATA attrData;
        if (GetFileAttributesExW(reinterpret_cast<const wchar_t*>(fullPath.utf16()), GetFileExInfoStandard, &attrData)) {
            QWriteLocker lock(&reader.m_dataLock);
            if (index < (int)reader.m_frns.size() && reader.m_frns[index] == frn) {
                reader.m_sizes[index] = (static_cast<uint64_t>(attrData.nFileSizeHigh) << 32) | attrData.nFileSizeLow;
                reader.m_timestamps[index] = filetimeToUnixMs((static_cast<int64_t>(attrData.ftLastWriteTime.dwHighDateTime) << 32) | attrData.ftLastWriteTime.dwLowDateTime);
                reader.m_attributes[index] = attrData.dwFileAttributes;
                reader.m_metadata_fetched[index] = 2;
                lock.unlock();
                emit reader.dataChanged(index);
                
                m_active_metadata_tasks.fetch_sub(1);
                processMetadataQueue();
                return;
            }
        }

        std::wstring rootPath = volume + L"\\";
        HANDLE hHint = CreateFileW(rootPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hHint != INVALID_HANDLE_VALUE) {
            FILE_ID_DESCRIPTOR id = { sizeof(FILE_ID_DESCRIPTOR), FileIdType };
            id.FileId.QuadPart = frn;
            HANDLE hFile = OpenFileById(hHint, &id, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, FILE_FLAG_BACKUP_SEMANTICS);
            if (hFile != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION bhfi;
                if (GetFileInformationByHandle(hFile, &bhfi)) {
                    QWriteLocker writeLock(&reader.m_dataLock);
                    if (index < (int)reader.m_frns.size() && reader.m_frns[index] == frn) {
                        reader.m_sizes[index] = (static_cast<uint64_t>(bhfi.nFileSizeHigh) << 32) | bhfi.nFileSizeLow;
                        reader.m_timestamps[index] = filetimeToUnixMs((static_cast<int64_t>(bhfi.ftLastWriteTime.dwHighDateTime) << 32) | bhfi.ftLastWriteTime.dwLowDateTime);
                        reader.m_attributes[index] = bhfi.dwFileAttributes;
                        reader.m_metadata_fetched[index] = 2;
                    }
                }
                CloseHandle(hFile);
            }
            CloseHandle(hHint);
        }

        QWriteLocker lock(&reader.m_dataLock);
        if (index < (int)reader.m_metadata_fetched.size() && reader.m_metadata_fetched[index] == 1) {
            if (reader.m_metadata_fetched[index] != 2) reader.m_metadata_fetched[index] = 0; 
        }
        lock.unlock();
        emit reader.dataChanged(index); 

        m_active_metadata_tasks.fetch_sub(1);
        processMetadataQueue();
    });
}

} // namespace ArcMeta
