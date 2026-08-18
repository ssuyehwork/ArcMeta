#include "HardwareInfoHelper.h"
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <QProcess>
#include <intrin.h>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <vector>

#ifndef StorageDeviceProtocolSpecificProperty
#define StorageDeviceProtocolSpecificProperty 51
#endif
#ifndef ProtocolTypeNvme
#define ProtocolTypeNvme 17
#endif
#ifndef NvmeDataTypeIdentify
#define NvmeDataTypeIdentify 0
#endif

#pragma pack(push, 1)
typedef struct _ATA_IDENTIFY_DATA {
    unsigned short Reserved1[10];
    unsigned short SerialNumber[10];      // Word 10-19
    unsigned short Reserved2[235];
} ATA_IDENTIFY_DATA;
#pragma pack(pop)

static QString formatAtaString(const unsigned short* data, int wordLen) {
    QByteArray ba;
    for (int i = 0; i < wordLen; ++i) {
        ba.append((char)(data[i] >> 8));
        ba.append((char)(data[i] & 0xFF));
    }
    return QString::fromLatin1(ba).trimmed();
}

QString HardwareInfoHelper::getDiskPhysicalSerialNumberByDrive(const QString& drivePath) {
    // 2026-03-xx 按照用户要求：核心重构，支持任意驱动器（C: D: 等）定位物理硬盘 SN。
    // 这允许程序识别移动硬盘的物理 ID，从而实现硬盘级别的跨设备使用。
    QString cleanDrive = drivePath.trimmed();
    if (cleanDrive.endsWith("\\")) cleanDrive.chop(1);
    if (cleanDrive.contains(":")) cleanDrive = cleanDrive.left(2);
    
    QString volumePath = "\\\\.\\" + cleanDrive;
    QElapsedTimer timer;
    timer.start();
    
    HANDLE hVolume = CreateFileW((LPCWSTR)volumePath.utf16(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) {
        qWarning() << "[HardwareInfo-PERF] 无法打开卷:" << volumePath << "耗时:" << timer.elapsed() << "ms";
        return "";
    }

    STORAGE_DEVICE_NUMBER sdn;
    DWORD br = 0;
    if (!DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &br, NULL)) {
        CloseHandle(hVolume);
        return "";
    }
    CloseHandle(hVolume);

    QString physicalPath = QString("\\\\.\\PhysicalDrive%1").arg(sdn.DeviceNumber);
    HANDLE hDisk = CreateFileW((LPCWSTR)physicalPath.utf16(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) {
        hDisk = CreateFileW((LPCWSTR)physicalPath.utf16(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (hDisk == INVALID_HANDLE_VALUE) return "";

    QString cachedSerial;
    // 1. 尝试 NVMe
    struct NvmeQueryBuffer {
        STORAGE_PROPERTY_QUERY Query;
        STORAGE_PROTOCOL_SPECIFIC_DATA ProtocolData;
        BYTE IdentifyBuffer[4096];
    } nvmeReq = {};
    nvmeReq.Query.PropertyId = static_cast<STORAGE_PROPERTY_ID>(StorageDeviceProtocolSpecificProperty);
    nvmeReq.Query.QueryType = PropertyStandardQuery;
    nvmeReq.ProtocolData.ProtocolType = static_cast<STORAGE_PROTOCOL_TYPE>(ProtocolTypeNvme);
    nvmeReq.ProtocolData.DataType = NvmeDataTypeIdentify;
    nvmeReq.ProtocolData.ProtocolDataRequestValue = 1;
    nvmeReq.ProtocolData.ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    nvmeReq.ProtocolData.ProtocolDataLength = 4096;

    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &nvmeReq, sizeof(nvmeReq), &nvmeReq, sizeof(nvmeReq), &br, NULL)) {
        cachedSerial = QString::fromLatin1((const char*)nvmeReq.IdentifyBuffer + 4, 20).trimmed();
    }

    // 2. 尝试 SATA/SMART
    if (cachedSerial.isEmpty()) {
        SENDCMDINPARAMS sci = {0};
        sci.irDriveRegs.bCommandReg = 0xEC; 
        struct {
            SENDCMDOUTPARAMS out;
            BYTE buffer[512];
        } smartOut = {0};
        if (DeviceIoControl(hDisk, SMART_RCV_DRIVE_DATA, &sci, sizeof(sci), &smartOut, sizeof(smartOut), &br, NULL)) {
            ATA_IDENTIFY_DATA* pData = (ATA_IDENTIFY_DATA*)smartOut.buffer;
            cachedSerial = formatAtaString(pData->SerialNumber, 10);
        }
    }

    // 3. 通用保底
    if (cachedSerial.isEmpty()) {
        STORAGE_PROPERTY_QUERY commonQuery = {StorageDeviceProperty, PropertyStandardQuery};
        STORAGE_DEVICE_DESCRIPTOR commonDesc = {0};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &commonQuery, sizeof(commonQuery), &commonDesc, sizeof(commonDesc), &br, NULL)) {
            std::vector<char> buf(commonDesc.Size);
            if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &commonQuery, sizeof(commonQuery), buf.data(), (DWORD)buf.size(), &br, NULL)) {
                STORAGE_DEVICE_DESCRIPTOR* pf = (STORAGE_DEVICE_DESCRIPTOR*)buf.data();
                if (pf->SerialNumberOffset > 0) cachedSerial = QString::fromLatin1(buf.data() + pf->SerialNumberOffset).trimmed();
            }
        }
    }

    CloseHandle(hDisk);
    return cachedSerial;
}

QString HardwareInfoHelper::getCDiskPhysicalSerialNumber() {
    static QString cachedC;
    if (cachedC.isEmpty()) cachedC = getDiskPhysicalSerialNumberByDrive("C:");
    return cachedC;
}

QString HardwareInfoHelper::getAppDrivePhysicalSerialNumber() {
    // 2026-03-xx 按照用户要求：动态识别程序当前路径所在的物理硬盘 SN。
    // 这支持了软件在移动硬盘环境下自识别，实现“一盘走天下”的硬件绑定通行证。
    static QString cachedApp;
    if (cachedApp.isEmpty()) {
        QString path = QCoreApplication::applicationDirPath();
        if (path.contains(":")) {
            QString drive = path.left(2);
            cachedApp = getDiskPhysicalSerialNumberByDrive(drive);
        }
    }
    return cachedApp;
}

QString HardwareInfoHelper::getDiskPhysicalSerialNumber() {
    // 2026-03-xx 兼容性接口：默认返回 C 盘 SN 以维持旧版逻辑稳定性。
    return getCDiskPhysicalSerialNumber();
}

QString HardwareInfoHelper::getBoardSerialNumber() {
    // 2026-03-xx 按照用户要求：通过 wmic 逻辑平替，获取主板 UUID/序列号
    // 实际生产环境下建议使用原生 SMBIOS 解析，此处采用高效的命令行包装作为第一阶段实现
    static QString cachedBoard;
    if (!cachedBoard.isEmpty()) return cachedBoard;

    QProcess process;
    process.start("wmic", QStringList() << "baseboard" << "get" << "serialnumber");
    if (process.waitForFinished()) {
        QString result = QString::fromLocal8Bit(process.readAllStandardOutput());
        QStringList lines = result.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        if (lines.size() >= 2) {
            cachedBoard = lines[1].trimmed();
        }
    }
    
    if (cachedBoard.isEmpty() || cachedBoard.toUpper().contains("TO BE FILLED")) {
        // 尝试获取 UUID 作为备选
        process.start("wmic", QStringList() << "csproduct" << "get" << "uuid");
        if (process.waitForFinished()) {
            QString result = QString::fromLocal8Bit(process.readAllStandardOutput());
            QStringList lines = result.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
            if (lines.size() >= 2) cachedBoard = lines[1].trimmed();
        }
    }
    return cachedBoard;
}

QString HardwareInfoHelper::getCpuId() {
    // 2026-03-xx 按照用户要求：使用 CPUID 指令提取处理器唯一特征
    static QString cachedCpuId;
    if (!cachedCpuId.isEmpty()) return cachedCpuId;

    int cpuInfo[4];
    __cpuid(cpuInfo, 1); // EAX=1: Processor Info and Feature Bits
    
    // 拼接 EAX 和 EDX 作为核心特征值
    cachedCpuId = QString("%1%2")
                  .arg(cpuInfo[0], 8, 16, QChar('0'))
                  .arg(cpuInfo[3], 8, 16, QChar('0')).toUpper();
    
    return cachedCpuId;
}

QString HardwareInfoHelper::getBiosSerialNumber() {
    // 2026-05-04 按照用户要求：获取 BIOS 序列号
    static QString cachedBiosSn;
    if (!cachedBiosSn.isEmpty()) return cachedBiosSn;

    QProcess process;
    process.start("wmic", QStringList() << "bios" << "get" << "serialnumber");
    process.waitForFinished(5000);
    QString output = process.readAllStandardOutput();
    
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    if (lines.size() >= 2) {
        QString serialLine = lines[1].trimmed();
        if (!serialLine.isEmpty() && serialLine != "SerialNumber") {
            cachedBiosSn = serialLine;
        }
    }
    
    return cachedBiosSn;
}

QStringList HardwareInfoHelper::getAllDiskSerialNumbers() {
    // 2026-05-04 按照用户要求：获取所有硬盘的物理序列号列表
    static QStringList cachedDiskSerials;
    if (!cachedDiskSerials.isEmpty()) return cachedDiskSerials;

    QProcess process;
    process.start("wmic", QStringList() << "diskdrive" << "get" << "serialnumber");
    process.waitForFinished(5000);
    QString output = process.readAllStandardOutput();
    
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) { // 跳过标题行
        QString serial = lines[i].trimmed();
        if (!serial.isEmpty() && serial != "SerialNumber") {
            cachedDiskSerials << serial;
        }
    }
    
    return cachedDiskSerials;
}

QStringList HardwareInfoHelper::getAllMacAddresses() {
    // 2026-05-04 按照用户要求：获取所有网络适配器的 MAC 地址列表
    static QStringList cachedMacAddresses;
    if (!cachedMacAddresses.isEmpty()) return cachedMacAddresses;

    QProcess process;
    process.start("getmac", QStringList() << "/fo" << "csv" << "/v");
    process.waitForFinished(5000);
    QString output = process.readAllStandardOutput();
    
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QStringList parts = line.split(',');
        if (parts.size() >= 2) {
            QString mac = parts[0].trimmed();
            mac = mac.remove('"').trimmed(); // 移除引号
            if (!mac.isEmpty() && mac != "Physical Address") {
                // 标准化 MAC 地址格式为大写并添加冒号分隔符
                if (mac.length() == 12) {
                    QString formattedMac = mac.toUpper();
                    formattedMac.insert(2, ':');
                    formattedMac.insert(5, ':');
                    formattedMac.insert(8, ':');
                    formattedMac.insert(11, ':');
                    formattedMac.insert(14, ':');
                    cachedMacAddresses << formattedMac;
                }
            }
        }
    }
    
    return cachedMacAddresses;
}
