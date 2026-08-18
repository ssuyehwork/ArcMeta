#ifndef HARDWAREINFOHELPER_H
#define HARDWAREINFOHELPER_H

#include <QString>

class HardwareInfoHelper {
public:
    /**
     * @brief 获取 Windows 系统主分区（C 盘）所在的物理磁盘序列号
     * @return 物理序列号字符串。
     */
    static QString getCDiskPhysicalSerialNumber();

    /**
     * @brief 获取程序当前运行目录所在的物理磁盘序列号
     * @return 物理序列号字符串。如果获取失败，返回空字符串。
     */
    static QString getAppDrivePhysicalSerialNumber();

    /**
     * @brief [通用] 获取指定驱动器路径（如 C: 或 D:）对应的物理磁盘序列号
     * @param drivePath 驱动器路径，如 "C:" 或 "D:"
     */
    static QString getDiskPhysicalSerialNumberByDrive(const QString& drivePath);

    // [DEPRECATED] 2026-03-xx 按照用户要求重构，建议使用上述更精准的接口
    static QString getDiskPhysicalSerialNumber();

    /**
     * @brief 2026-03-xx 按照用户要求：获取主板 UUID (SMBIOS)
     */
    static QString getBoardSerialNumber();

    /**
     * @brief 2026-03-xx 按照用户要求：获取 CPUID 序列号
     */
    static QString getCpuId();

    /**
     * @brief 获取 BIOS 序列号
     */
    static QString getBiosSerialNumber();

    /**
     * @brief 获取所有硬盘的物理序列号列表
     */
    static QStringList getAllDiskSerialNumbers();

    /**
     * @brief 获取所有网络适配器的 MAC 地址列表
     */
    static QStringList getAllMacAddresses();
};

#endif // HARDWAREINFOHELPER_H
