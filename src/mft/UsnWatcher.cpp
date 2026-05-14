#include "UsnWatcher.h"
#include <QDebug>
#include <winioctl.h>
#include <QDateTime>

namespace ArcMeta {

UsnWatcher::UsnWatcher(const QString& volume, unsigned __int64 startUsn, QObject* parent)
    : QThread(parent), m_volume(volume), m_startUsn(startUsn), m_stopRequested(false) {
    
    QString volPath = m_volume.toUpper();
    if (volPath.endsWith('\\')) volPath.chop(1);
    if (!volPath.startsWith("\\\\.\\")) volPath = "\\\\.\\" + volPath;

    m_hVolume = CreateFileW(reinterpret_cast<const wchar_t*>(volPath.utf16()),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        qDebug() << "[UsnWatcher] 错误：无法打开卷句柄" << volPath;
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

    // 1. 获取 Journal ID
    USN_JOURNAL_DATA journalData;
    DWORD bytesReturned;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        return;
    }

    // 2. 如果 startUsn 为 0，则默认从当前点位开始
    READ_USN_JOURNAL_DATA readData;
    readData.StartUsn = (m_startUsn == 0) ? journalData.NextUsn : m_startUsn;
    readData.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | 
                         USN_REASON_RENAME_NEW_NAME | USN_REASON_BASIC_INFO_CHANGE;
    readData.ReturnOnlyOnClose = 0;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalData.UsnJournalID;

    const int bufferSize = 64 * 1024;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bufferSize]);

    while (!m_stopRequested.load()) {
        if (!DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.get(), static_cast<DWORD>(bufferSize), &bytesReturned, NULL)) {
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        QList<UsnChange> changes;
        USN* pNextUsn = reinterpret_cast<USN*>(buffer.get());
        uint8_t* pRecord = buffer.get() + sizeof(USN);
        uint8_t* pEnd = buffer.get() + bytesReturned;

        while (pRecord < pEnd) {
            USN_RECORD* record = reinterpret_cast<USN_RECORD*>(pRecord);
            
            UsnChange change;
            change.frn = record->FileReferenceNumber;
            change.parentFrn = record->ParentFileReferenceNumber;
            change.name = QString::fromUtf16(reinterpret_cast<const char16_t*>(pRecord + record->FileNameOffset), record->FileNameLength / 2);
            change.attributes = record->FileAttributes;
            change.size = 0;

            if (record->Reason & USN_REASON_FILE_CREATE) change.type = UsnChange::Created;
            else if (record->Reason & USN_REASON_FILE_DELETE) change.type = UsnChange::Deleted;
            else if (record->Reason & USN_REASON_RENAME_NEW_NAME) change.type = UsnChange::Renamed;
            else change.type = UsnChange::Modified;

            changes.append(change);
            pRecord += record->RecordLength;
        }

        if (!changes.isEmpty()) {
            // 2026-05-09 极致性能：直接抛出变更信号，由高性能 SoA 引擎处理，彻底废弃旧版 MftReader 桥接逻辑
            emit changesDetected(changes);
        }
        
        readData.StartUsn = *pNextUsn;
    }
}

} // namespace ArcMeta
