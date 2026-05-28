#include "UsnWatcher.h"
#include <QDebug>
#include <winioctl.h>
#include <vector>

namespace ArcMeta {

UsnWatcher::UsnWatcher(const std::wstring& volume, uint64_t startUsn, QObject* parent)
    : QThread(parent), m_volume(volume), m_lastUsn(startUsn), m_stopRequested(false) {
    
    std::wstring devPath = L"\\\\.\\" + m_volume;
    if (devPath.back() == L'\\') devPath.pop_back();

    m_hVolume = CreateFileW(devPath.c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                           NULL);
    
    m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

UsnWatcher::~UsnWatcher() {
    stop();
    if (m_hVolume != INVALID_HANDLE_VALUE) CloseHandle(m_hVolume);
    if (m_hEvent) CloseHandle(m_hEvent);
}

void UsnWatcher::stop() {
    m_stopRequested.store(true);
    if (m_hEvent) SetEvent(m_hEvent);
    if (isRunning()) wait();
}

void UsnWatcher::run() {
    if (m_hVolume == INVALID_HANDLE_VALUE || !m_hEvent) return;

    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = m_hEvent;

    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        return;
    }

    if (m_lastUsn == 0) m_lastUsn = journalData.NextUsn;

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn = m_lastUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = 0;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalData.UsnJournalID;

    const int bufferSize = 128 * 1024;
    std::vector<uint8_t> buffer(bufferSize);

    while (!m_stopRequested.load()) {
        ResetEvent(m_hEvent);
        BOOL ok = DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.data(), bufferSize, &bytesReturned, &overlapped);

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(m_hEvent, INFINITE) == WAIT_OBJECT_0) {
                if (m_stopRequested.load()) break;
                if (!GetOverlappedResult(m_hVolume, &overlapped, &bytesReturned, FALSE)) continue;
            }
        } else if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_JOURNAL_DELETE_IN_PROGRESS || err == ERROR_JOURNAL_NOT_ACTIVE) {
                emit journalInvalidated();
                break;
            }
            msleep(100);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            msleep(100);
            continue;
        }

        uint8_t* pRecord = buffer.data() + sizeof(USN);
        uint8_t* pEnd = buffer.data() + bytesReturned;

        while (pRecord < pEnd) {
            USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRecord);
            if (header->RecordLength == 0) break;

            // 2026-06-xx 物理同步：发射信号，由 SyncManager 转发至 MftReader
            emit recordReceived(QByteArray(reinterpret_cast<const char*>(pRecord), header->RecordLength));

            pRecord += header->RecordLength;
        }

        readData.StartUsn = *reinterpret_cast<USN*>(buffer.data());
        m_lastUsn = readData.StartUsn;
    }
}

} // namespace ArcMeta
