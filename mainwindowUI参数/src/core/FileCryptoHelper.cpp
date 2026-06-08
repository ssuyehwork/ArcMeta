#include "FileCryptoHelper.h"
#include "AES.h"
#include <QDebug>
#include <QSettings>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QThread>
#include <cstdint>

#define MAGIC_HEADER_SIZE 16
#define SALT_SIZE 16
#define IV_SIZE 16
#define KEY_SIZE 32
#define PBKDF2_ITERATIONS 5000 

static const char SHELL_MAGIC[MAGIC_HEADER_SIZE] = {'R', 'A', 'P', 'I', 'D', 'N', 'O', 'T', 'E', 'S', 'S', 'H', 'E', 'L', 'L', '!'};

QByteArray FileCryptoHelper::deriveKey(const QString& password, const QByteArray& salt) {
    QByteArray key = password.toUtf8();
    for (int i = 0; i < PBKDF2_ITERATIONS; ++i) {
        key = QCryptographicHash::hash(key + salt, QCryptographicHash::Sha256);
    }
    return key;
}

bool FileCryptoHelper::decryptFileLegacy(const QString& sourcePath, const QString& destPath, const QString& password) {
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly)) return false;

    QByteArray salt = src.read(SALT_SIZE);
    QByteArray iv = src.read(IV_SIZE);
    QByteArray encryptedData = src.readAll();
    src.close();

    if (salt.size() != SALT_SIZE || iv.size() != IV_SIZE) return false;

    QByteArray key = deriveKey(password, salt);

    AES aes(AES::AES_256);
    std::vector<std::uint8_t> input((const std::uint8_t*)encryptedData.constData(), (const std::uint8_t*)encryptedData.constData() + encryptedData.size());
    std::vector<std::uint8_t> keyVec((const std::uint8_t*)key.constData(), (const std::uint8_t*)key.constData() + key.size());
    std::vector<std::uint8_t> ivVec((const std::uint8_t*)iv.constData(), (const std::uint8_t*)iv.constData() + iv.size());

    std::vector<std::uint8_t> decrypted = aes.decryptCBC(input, keyVec, ivVec);
    if (decrypted.empty()) return false;

    QFile dest(destPath);
    if (!dest.open(QIODevice::WriteOnly)) return false;
    dest.write((const char*)decrypted.data(), decrypted.size());
    dest.close();

    return true;
}

bool FileCryptoHelper::encryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password) {
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly)) return false;
    QByteArray srcData = src.readAll();
    src.close();

    QByteArray salt(SALT_SIZE, 0);
    QByteArray iv(IV_SIZE, 0);
    for(int i=0; i<SALT_SIZE; ++i) salt[i] = (char)QRandomGenerator::global()->bounded(256);
    for(int i=0; i<IV_SIZE; ++i) iv[i] = (char)QRandomGenerator::global()->bounded(256);

    QByteArray key = deriveKey(password, salt);

    AES aes(AES::AES_256);
    std::vector<std::uint8_t> input((const std::uint8_t*)srcData.constData(), (const std::uint8_t*)srcData.constData() + srcData.size());
    std::vector<std::uint8_t> keyVec((const std::uint8_t*)key.constData(), (const std::uint8_t*)key.constData() + key.size());
    std::vector<std::uint8_t> ivVec((const std::uint8_t*)iv.constData(), (const std::uint8_t*)iv.constData() + iv.size());
    
    std::vector<std::uint8_t> encrypted = aes.encryptCBC(input, keyVec, ivVec);

    // [SAFETY] 采用原子操作：先写入临时文件，成功后再重命名，防止加密中断导致主库损坏
    QString tempPath = destPath + ".writing.tmp";
    QFile dest(tempPath);
    if (!dest.open(QIODevice::WriteOnly)) return false;
    dest.write(SHELL_MAGIC, MAGIC_HEADER_SIZE);
    dest.write(salt);
    dest.write(iv);
    dest.write((const char*)encrypted.data(), encrypted.size());
    dest.flush();
    dest.close();

    if (QFile::exists(destPath)) {
        if (!QFile::remove(destPath)) {
            qCritical() << "[Crypto] 无法移除旧文件，覆盖失败:" << destPath;
            return false;
        }
    }

    if (!QFile::rename(tempPath, destPath)) {
        qCritical() << "[Crypto] 原子重命名失败:" << tempPath << "->" << destPath;
        return false;
    }

    return true;
}

bool FileCryptoHelper::decryptFileWithShell(const QString& sourcePath, const QString& destPath, const QString& password) {
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly)) return false;

    QByteArray header = src.read(MAGIC_HEADER_SIZE);
    if (header != QByteArray(SHELL_MAGIC, MAGIC_HEADER_SIZE)) {
        src.close();
        return false;
    }

    QByteArray salt = src.read(SALT_SIZE);
    QByteArray iv = src.read(IV_SIZE);
    QByteArray encryptedData = src.readAll();
    src.close();

    if (salt.size() != SALT_SIZE || iv.size() != IV_SIZE) return false;

    QByteArray key = deriveKey(password, salt);

    AES aes(AES::AES_256);
    std::vector<std::uint8_t> input((const std::uint8_t*)encryptedData.constData(), (const std::uint8_t*)encryptedData.constData() + encryptedData.size());
    std::vector<std::uint8_t> keyVec((const std::uint8_t*)key.constData(), (const std::uint8_t*)key.constData() + key.size());
    std::vector<std::uint8_t> ivVec((const std::uint8_t*)iv.constData(), (const std::uint8_t*)iv.constData() + iv.size());

    std::vector<std::uint8_t> decrypted = aes.decryptCBC(input, keyVec, ivVec);
    if (decrypted.empty()) return false;

    QFile dest(destPath);
    if (!dest.open(QIODevice::WriteOnly)) return false;
    dest.write((const char*)decrypted.data(), decrypted.size());
    dest.close();

    return true;
}

QString FileCryptoHelper::getCombinedKeyBySN(const QString& sn) {
    // 2026-03-xx 按照用户最高要求：彻底移除硬件指纹绑定
    // 使用传入的标识符（现已统一为固定常量）进行哈希，确保密钥可移植
    QString hardcode = "RapidNotes-Genuine-Barrier-2026";
    return QCryptographicHash::hash((hardcode + sn).toUtf8(), QCryptographicHash::Sha256).toHex();
}

QString FileCryptoHelper::getCombinedKey() {
    // 2026-03-xx 重定向到固定密钥，不再调用 HardwareInfoHelper
    return getCombinedKeyBySN("RAPID_MANAGER_PERMANENT_KEY_2026");
}

QString FileCryptoHelper::getLegacyCombinedKey() {
    // 2026-03-xx 按照用户要求：不再调用系统环境指纹，仅尝试使用固定盐值解密旧数据
    QString hardcode = "RapidNotes-Genuine-Barrier-2026";
    return QCryptographicHash::hash(hardcode.toUtf8(), QCryptographicHash::Sha256).toHex();
}

bool FileCryptoHelper::secureDelete(const QString& filePath) {
    QFile file(filePath);
    if (!file.exists()) return true;
    
    // 尝试多次删除 (处理 SQLite 延迟释放)
    for (int retry = 0; retry < 3; ++retry) {
        if (file.open(QIODevice::ReadWrite)) {
            qint64 size = file.size();
            if (size > 0) {
                // 2026-03-15 [PERF] 增大填充块到 64KB，提升大文件（40MB+）的擦除速度。
                QByteArray junk(65536, 0);
                for (qint64 i = 0; i < size; i += junk.size()) {
                    for(int j=0; j<junk.size(); ++j) junk[j] = (char)QRandomGenerator::global()->bounded(256);
                    file.write(junk);
                }
                file.flush();
            }
            file.close();
            if (QFile::remove(filePath)) return true;
        }
        QThread::msleep(100);
    }
    
    return QFile::remove(filePath);
}
