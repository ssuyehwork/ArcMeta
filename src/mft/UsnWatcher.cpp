#include "UsnWatcher.h"
#include "MftReader.h"
#include <QDebug>
#include <winioctl.h>
#include <cstring> // 引入 std::memcpy

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
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        qDebug() << "[UsnWatcher] 错误：无法打开卷句柄" << QString::fromStdWString(devPath);
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
    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        return;
    }

    // 2. 离线追平逻辑：若 m_lastUsn 为 0，从当前 NextUsn 开始
    if (m_lastUsn == 0) {
        m_lastUsn = journalData.NextUsn;
    }

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn = m_lastUsn;
    readData.ReasonMask = 0xFFFFFFFF; // 监控所有原因
    readData.ReturnOnlyOnClose = 0;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalData.UsnJournalID;

    // 根据规范：使用 std::unique_ptr<uint8_t[]> 管理缓冲区
    const int bufferSize = 128 * 1024;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bufferSize]);

    while (!m_stopRequested.load()) {
        if (!DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.get(), bufferSize, &bytesReturned, NULL)) {
            DWORD err = GetLastError();

            // 引入自愈探测：若日志删除、未激活、参数无效，或由于覆盖被截断 (1181: ERROR_JOURNAL_ENTRY_DELETED)
            if (err == ERROR_JOURNAL_DELETE_IN_PROGRESS ||
                err == ERROR_JOURNAL_NOT_ACTIVE ||
                err == ERROR_INVALID_PARAMETER ||
                err == ERROR_JOURNAL_ENTRY_DELETED) {

                qDebug() << "[UsnWatcher] 检测到 Journal 失效、截断或重建，执行自愈重置... 错误码:" << err << QString::fromStdWString(m_volume);

                // 必须重新查询 Journal 以更新 UsnJournalID
                USN_JOURNAL_DATA_V0 newJournalData;
                DWORD queryBytes;
                if (DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &newJournalData, sizeof(newJournalData), &queryBytes, NULL)) {
                    readData.UsnJournalID = newJournalData.UsnJournalID;
                    // 自愈重置策略：由于历史记录已丢失，直接定位到最新的 NextUsn 处开始增量监控
                    readData.StartUsn = newJournalData.NextUsn;
                    m_lastUsn = readData.StartUsn;
                    qDebug() << "[UsnWatcher] 自愈成功，新 JournalID:" << newJournalData.UsnJournalID << "新 StartUsn:" << readData.StartUsn;
                } else {
                    readData.StartUsn = 0;
                    m_lastUsn = 0;
                }
            }
            
            // 出错时小步长等待，确保可及时退出
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            // 无新数据，小步长等待
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        uint8_t* pRecord = buffer.get() + sizeof(USN);
        uint8_t* pEnd = buffer.get() + bytesReturned;

        // 警告：updateBatch 中仅存储指向临时缓冲区 buffer 的原始指针。
        // MftReader::updateEntriesFromUsnBatch 必须同步处理 these 指针指向的内容，不得进行异步延迟访问，否则下一轮循环 buffer 会被重写。
        std::vector<uint8_t*> updateBatch;
        while (pRecord < pEnd && !m_stopRequested.load()) {
            USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRecord);
            
            if (header->MajorVersion == 2 || header->MajorVersion == 3) {
                uint32_t reason = 0;
                uint64_t frn = 0;

                // 安全解析：规避严格别名规则漏洞
                if (header->MajorVersion == 2) {
                    USN_RECORD_V2* v2 = reinterpret_cast<USN_RECORD_V2*>(pRecord);
                    reason = v2->Reason;
                    frn = v2->FileReferenceNumber;
                } else {
                    USN_RECORD_V3* v3 = reinterpret_cast<USN_RECORD_V3*>(pRecord);
                    reason = v3->Reason;
                    std::memcpy(&frn, &v3->FileReferenceNumber, sizeof(uint64_t)); // 安全截取 128 位文件 ID 字段的前 8 字节
                }
                
                if (reason & (USN_REASON_FILE_CREATE | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_RENAME_NEW_NAME)) {
                    updateBatch.push_back(pRecord);
                } else if (reason & USN_REASON_FILE_DELETE) {
                    MftReader::instance().removeEntryByFrn(m_volume, frn);
                }
            }
            pRecord += header->RecordLength;
        }

        if (!updateBatch.empty()) {
            // 工业级 UI 饥饿修复：分片分批投递，让出 CPU
            const size_t chunkSize = 1000;
            for (size_t i = 0; i < updateBatch.size(); i += chunkSize) {
                if (m_stopRequested.load()) break;
                size_t end = (std::min)(i + chunkSize, updateBatch.size());
                std::vector<uint8_t*> chunk(updateBatch.begin() + i, updateBatch.begin() + end);
                MftReader::instance().updateEntriesFromUsnBatch(chunk, m_volume);
                
                // 强制释放 CPU 时间片，保障 GUI 线程渲染
                QThread::msleep(5); 
            }
        }

        // 更新起始 USN 为本次读取后的 NextUsn
        readData.StartUsn = *reinterpret_cast<USN*>(buffer.get());
        m_lastUsn = readData.StartUsn;
    }
}

// 修正后的处理函数，使用通用 uint8_t* 规避 V2 到 V3 的非安全转型
void UsnWatcher::handleRecord(uint8_t* pRecord) {
    if (!pRecord) return;
    USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRecord);
    uint32_t reason = 0;
    uint64_t frn = 0;

    if (header->MajorVersion == 2) {
        USN_RECORD_V2* v2 = reinterpret_cast<USN_RECORD_V2*>(pRecord);
        reason = v2->Reason;
        frn = v2->FileReferenceNumber;
    } else if (header->MajorVersion == 3) {
        USN_RECORD_V3* v3 = reinterpret_cast<USN_RECORD_V3*>(pRecord);
        reason = v3->Reason;
        std::memcpy(&frn, &v3->FileReferenceNumber, sizeof(uint64_t));
    } else {
        return;
    }

    if (reason & (USN_REASON_FILE_CREATE | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_RENAME_NEW_NAME)) {
        MftReader::instance().updateEntryFromUsn(pRecord, m_volume);
    }
    else if (reason & USN_REASON_FILE_DELETE) {
        MftReader::instance().removeEntryByFrn(m_volume, frn);
    }
}

} // namespace ArcMeta
