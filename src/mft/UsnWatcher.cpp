#include "UsnWatcher.h"
#include "MftReader.h"
#include <QDebug>
#include <QDir>
#include <winioctl.h>

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
            // 方案二：引入 USN 自愈探测。若 Journal 失效或被覆盖，执行重置
            if (err == ERROR_JOURNAL_DELETE_IN_PROGRESS || err == ERROR_JOURNAL_NOT_ACTIVE || err == ERROR_INVALID_PARAMETER) {
                qDebug() << "[UsnWatcher] 检测到 Journal 失效，执行自愈重置..." << QString::fromStdWString(m_volume);
                readData.StartUsn = 0;
                m_lastUsn = 0;
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

        std::vector<uint8_t*> updateBatch; // 存储原始指针以保留版本信息
        while (pRecord < pEnd) {
            USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRecord);
            
            // 工业级优化：优先采用批量处理模式
            if (header->MajorVersion == 2 || header->MajorVersion == 3) {
                uint32_t reason = (header->MajorVersion == 2) ? 
                    reinterpret_cast<USN_RECORD_V2*>(pRecord)->Reason : 
                    reinterpret_cast<USN_RECORD_V3*>(pRecord)->Reason;
                
                if (reason & (USN_REASON_FILE_CREATE | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_RENAME_NEW_NAME)) {
                    updateBatch.push_back(pRecord);
                } else if (reason & USN_REASON_FILE_DELETE) {
                    uint64_t frn = (header->MajorVersion == 2) ? 
                        reinterpret_cast<USN_RECORD_V2*>(pRecord)->FileReferenceNumber : 
                        *reinterpret_cast<uint64_t*>(&reinterpret_cast<USN_RECORD_V3*>(pRecord)->FileReferenceNumber);
                    MftReader::instance().removeEntryByFrn(m_volume, frn);
                    
                    int driveIdx = 0;
                    const auto drives = QDir::drives();
                    for (int i = 0; i < drives.size(); ++i) {
                        QString d = drives[i].absolutePath().left(2).toUpper();
                        if (QString::fromStdWString(m_volume).toUpper().startsWith(d)) {
                            driveIdx = i;
                            break;
                        }
                    }
                    uint64_t key = ((uint64_t)driveIdx << 48) | (frn & 0x0000FFFFFFFFFFFFull);
                    emit MftReader::instance().entryRemoved(key);
                }
            }
            pRecord += header->RecordLength;
        }

        if (!updateBatch.empty()) {
            // 2026-06-xx 工业级 UI 饥饿修复：
            // 如果批次过大，进行分片处理，并在分片间强制释放写锁，给 GUI 线程留出渲染时间
            const size_t chunkSize = 1000;
            for (size_t i = 0; i < updateBatch.size(); i += chunkSize) {
                if (m_stopRequested.load()) break;
                size_t end = (std::min)(i + chunkSize, updateBatch.size());
                std::vector<uint8_t*> chunk(updateBatch.begin() + i, updateBatch.begin() + end);
                MftReader::instance().updateEntriesFromUsnBatch(chunk, m_volume);
                
                for (uint8_t* pRaw : chunk) {
                    USN_RECORD_COMMON_HEADER* header =
                        reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRaw);
                    uint32_t reason = 0;
                    uint64_t frn = 0;
                    int driveIdx = 0;

                    if (header->MajorVersion == 2) {
                        auto* r = reinterpret_cast<USN_RECORD_V2*>(pRaw);
                        reason = r->Reason;
                        frn = r->FileReferenceNumber;
                    } else if (header->MajorVersion == 3) {
                        auto* r = reinterpret_cast<USN_RECORD_V3*>(pRaw);
                        reason = r->Reason;
                        frn = *reinterpret_cast<uint64_t*>(&r->FileReferenceNumber);
                    }

                    const auto drives = QDir::drives();
                    for (int j = 0; j < drives.size(); ++j) {
                        QString d = drives[j].absolutePath().left(2).toUpper();
                        if (QString::fromStdWString(m_volume).toUpper().startsWith(d)) {
                            driveIdx = j;
                            break;
                        }
                    }

                    uint64_t key = ((uint64_t)driveIdx << 48) | (frn & 0x0000FFFFFFFFFFFFull);

                    if (reason & USN_REASON_FILE_DELETE) {
                        emit MftReader::instance().entryRemoved(key);
                    } else if (reason & USN_REASON_RENAME_NEW_NAME) {
                        emit MftReader::instance().entryUpdated(key);
                    } else if (reason & USN_REASON_FILE_CREATE) {
                        emit MftReader::instance().entryAdded(key);
                    }
                }

                // 强制释放 CPU 时间片，解决长时挂起（休眠）唤醒后的“未响应”现象
                QThread::msleep(5); 
            }
        }

        // 更新起始 USN 为本次读取后的 NextUsn
        readData.StartUsn = *reinterpret_cast<USN*>(buffer.get());
        m_lastUsn = readData.StartUsn;
    }
}

} // namespace ArcMeta
