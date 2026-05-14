#include "UsnWatcher.h"
#include "MftReader.h"
#include <winioctl.h>
#include <QDebug>

namespace ArcMeta {

UsnWatcher::UsnWatcher(const std::wstring& volume, uint64_t startUsn, QObject* parent)
    : QThread(parent), m_volume(volume), m_lastUsn(startUsn), m_stopRequested(false), m_hVolume(INVALID_HANDLE_VALUE) {
    
    std::wstring devicePath = L"\\\\.\\" + m_volume;
    m_hVolume = CreateFileW(devicePath.c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        qDebug() << "[UsnWatcher] 错误：无法打开卷句柄" << QString::fromStdWString(devicePath);
    }
}

UsnWatcher::~UsnWatcher() {
    stop();
    if (m_hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hVolume);
        m_hVolume = INVALID_HANDLE_VALUE;
    }
}

void UsnWatcher::stop() {
    m_stopRequested.store(true);
    if (isRunning()) {
        wait();
    }
}

void UsnWatcher::run() {
    if (m_hVolume == INVALID_HANDLE_VALUE) return;

    // 1. 获取 Journal 数据
    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        return;
    }

    // 2. 离线追平逻辑
    if (m_lastUsn == 0) {
        m_lastUsn = journalData.NextUsn;
    }

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn       = m_lastUsn;
    readData.ReasonMask     = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = 0;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID   = journalData.UsnJournalID;

    const int bufferSize = 128 * 1024;
    std::vector<uint8_t> buffer(bufferSize);

    while (!m_stopRequested.load()) {
        if (!DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.data(), (DWORD)buffer.size(), &bytesReturned, NULL)) {
            // 发生错误或超时，小步长休眠
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            // 无新数据，小步长休眠
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        uint8_t* pRecord = buffer.data() + sizeof(USN);
        uint8_t* pEnd = buffer.data() + bytesReturned;

        while (pRecord < pEnd) {
            USN_RECORD_V2* record = (USN_RECORD_V2*)pRecord;
            handleRecord(record);
            pRecord += record->RecordLength;
        }

        readData.StartUsn = *(USN*)buffer.data();
        m_lastUsn = readData.StartUsn;
    }
}

void UsnWatcher::handleRecord(USN_RECORD_V2* pRecord) {
    // 按照任务要求，只更新 MftReader 内存，不操作数据库
    if (pRecord->Reason & USN_REASON_FILE_CREATE ||
        pRecord->Reason & USN_REASON_DATA_OVERWRITE ||
        pRecord->Reason & USN_REASON_BASIC_INFO_CHANGE) {
        MftReader::instance().updateEntryFromUsn(pRecord, m_volume);
    }
    else if (pRecord->Reason & USN_REASON_FILE_DELETE) {
        MftReader::instance().removeEntryByFrn(m_volume, pRecord->FileReferenceNumber);
    }
    else if (pRecord->Reason & USN_REASON_RENAME_NEW_NAME) {
        MftReader::instance().updateEntryFromUsn(pRecord, m_volume);
    }
}

} // namespace ArcMeta
