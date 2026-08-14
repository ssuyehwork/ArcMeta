#ifndef FILECRYPTOHELPER_H
#define FILECRYPTOHELPER_H

#include <QString>
#include <QByteArray>
#include <QFile>

class FileCryptoHelper {
public:
    // 三层架构专用：带魔数的壳加密/解密
    static bool encryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password);
    static bool decryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password);
    
    // 旧版解密 (Legacy): 不检查魔数
    static bool decryptFileLegacy(const QString& sourcePath, const QString& destPath, const QString& password);
    
    // 获取设备指纹与内置 Hardcode 结合的密钥 (默认 C 盘锁定)
    static QString getCombinedKey();

    /**
     * @brief 2026-03-xx [NEW] 根据传入的硬件序列号 (SN) 生成复合加密密钥
     * 这使得加密逻辑能够动态支持“C 盘”或“程序运行盘”双重锁定。
     */
    static QString getCombinedKeyBySN(const QString& sn);
    
    // [TRANSITION] 获取旧版基于 MachineGuid 的密钥，用于数据平滑迁移
    static QString getLegacyCombinedKey();

    // 安全删除文件（覆盖后再删除）
    static bool secureDelete(const QString& filePath);

private:
    static QByteArray deriveKey(const QString& password, const QByteArray& salt);
};

#endif // FILECRYPTOHELPER_H
