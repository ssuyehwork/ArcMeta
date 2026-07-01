#include "UsnWatcher.h"
#include "MftReader.h"
#include <QDebug>
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
    if (m_hVolume == INVALID_HANDLE_VALUE) {
        qDebug() << "[UsnWatcher] 卷句柄无效，线程退出:" << QString::fromStdWString(m_volume);
        return;
    }

    qDebug() << "[UsnWatcher] 线程已启动，正在查询 Journal:" << QString::fromStdWString(m_volume);

    // 1. 获取 Journal ID
    USN_JOURNAL_DATA_V0 journalData;
    DWORD bytesReturned;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &bytesReturned, NULL)) {
        qDebug() << "[UsnWatcher] FSCTL_QUERY_USN_JOURNAL 失败，错误码:" << GetLastError() << "卷:" << QString::fromStdWString(m_volume);
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
        BOOL success = DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.get(), bufferSize, &bytesReturned, NULL);

        static int loopCount = 0;
        if (++loopCount % 100 == 0) {
            qDebug() << "[UsnWatcher Post] 正在循环读取卷:" << QString::fromStdWString(m_volume) << "Success:" << success << "Bytes:" << bytesReturned << "NextStartUsn:" << readData.StartUsn;
        }

        if (!success) {
            DWORD err = GetLastError();
            qDebug() << "[UsnWatcher Post] FSCTL_READ_USN_JOURNAL 失败! 错误码:" << err << "卷:" << QString::fromStdWString(m_volume);
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
            // 无新数据
            for (int i = 0; i < 10 && !m_stopRequested.load(); ++i) msleep(50);
            continue;
        }

        qDebug() << "[UsnWatcher Post] 成功获取数据量:" << bytesReturned << "来自卷:" << QString::fromStdWString(m_volume);

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
                
                // 精准排查日志：感知层 (简单粗暴)
                {
                    WORD fileNameLength = (header->MajorVersion == 2) ?
                        reinterpret_cast<USN_RECORD_V2*>(pRecord)->FileNameLength :
                        reinterpret_cast<USN_RECORD_V3*>(pRecord)->FileNameLength;
                    WORD fileNameOffset = (header->MajorVersion == 2) ?
                        reinterpret_cast<USN_RECORD_V2*>(pRecord)->FileNameOffset :
                        reinterpret_cast<USN_RECORD_V3*>(pRecord)->FileNameOffset;
                    QString name = QString::fromUtf16(reinterpret_cast<const char16_t*>(pRecord + fileNameOffset), fileNameLength / 2);

                    // 粗暴日志：输出前 10 条任何文件的变化，证明解析在跑
                    static int totalRecordCount = 0;
                    if (++totalRecordCount <= 10) {
                        qDebug() << "[USN Raw] (DEBUG-SAMPLE) 发现记录:" << name << "Reason:" << QString::number(reason, 16);
                    }

                    // 修正：不区分大小写，确保 z:\ArcMeta.library_Z 等路径也能被捕获
                    if (name.contains("ArcMeta.Library_", Qt::CaseInsensitive)) {
                        qDebug() << "[USN Raw] 感知到目标路径变化:" << name << "Reason:" << QString::number(reason, 16);
                    }
                }

                if (reason & (USN_REASON_FILE_CREATE | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_RENAME_NEW_NAME)) {
                    updateBatch.push_back(pRecord);
                } else if (reason & USN_REASON_FILE_DELETE) {
                    uint64_t frn = (header->MajorVersion == 2) ? 
                        reinterpret_cast<USN_RECORD_V2*>(pRecord)->FileReferenceNumber : 
                        *reinterpret_cast<uint64_t*>(&reinterpret_cast<USN_RECORD_V3*>(pRecord)->FileReferenceNumber);
                    MftReader::instance().removeEntryByFrn(m_volume, frn);
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
                
                // 强制释放 CPU 时间片，解决长时挂起（休眠）唤醒后的“未响应”现象
                QThread::msleep(5); 
            }
        }

        // 更新起始 USN 为本次读取后的 NextUsn
        readData.StartUsn = *reinterpret_cast<USN*>(buffer.get());
        m_lastUsn = readData.StartUsn;
    }
}

void UsnWatcher::handleRecord(USN_RECORD_V2* pRecord) {
    USN_RECORD_COMMON_HEADER* header = reinterpret_cast<USN_RECORD_COMMON_HEADER*>(pRecord);
    uint32_t reason;
    uint64_t frn;

    if (header->MajorVersion == 2) {
        reason = pRecord->Reason;
        frn = pRecord->FileReferenceNumber;
    } else if (header->MajorVersion == 3) {
        USN_RECORD_V3* v3 = reinterpret_cast<USN_RECORD_V3*>(pRecord);
        reason = v3->Reason;
        frn = *reinterpret_cast<uint64_t*>(&v3->FileReferenceNumber);
    } else return;

    // 仅更新 MftReader 内存 SoA，不直接操作数据库
    if (reason & (USN_REASON_FILE_CREATE | USN_REASON_DATA_OVERWRITE | USN_REASON_BASIC_INFO_CHANGE)) {
        MftReader::instance().updateEntryFromUsn(reinterpret_cast<uint8_t*>(pRecord), m_volume);
    }
    else if (reason & USN_REASON_FILE_DELETE) {
        MftReader::instance().removeEntryByFrn(m_volume, frn);
    }
    else if (reason & USN_REASON_RENAME_NEW_NAME) {
        MftReader::instance().updateEntryFromUsn(reinterpret_cast<uint8_t*>(pRecord), m_volume);
    }
}

} // namespace ArcMeta
