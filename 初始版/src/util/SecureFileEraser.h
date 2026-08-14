#pragma once
#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#endif

namespace ArcMeta {
class SecureFileEraser {
public:
    static bool shredFile(const QString& path) {
        // 安全擦除逻辑：递归覆写
        QFileInfo info(path);
        if (info.isDir()) {
            QDir dir(path);
            for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                shredFile(path + "/" + entry);
            }
            return QDir().rmdir(path);
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadWrite)) return false;
            qint64 size = file.size();
            if (size > 0) {
                QByteArray buffer(65536, 0);
                for (int pass = 0; pass < 3; ++pass) { // 3 遍随机覆写
                    file.seek(0);
                    qint64 written = 0;
                    while (written < size) {
                        for (int i = 0; i < buffer.size(); ++i) {
                            buffer[i] = (char)QRandomGenerator::global()->bounded(256);
                        }
                        qint64 toWrite = qMin((qint64)buffer.size(), size - written);
                        file.write(buffer.data(), toWrite);
                        written += toWrite;
                    }
                    file.flush();
                    #ifdef Q_OS_WIN
                    HANDLE hFile = (HANDLE)_get_osfhandle(file.handle());
                    if (hFile != INVALID_HANDLE_VALUE) {
                        FlushFileBuffers(hFile); // 强制刷新扇区落盘
                    }
                    #endif
                }
            }
            file.close();
            return QFile::remove(path);
        }
    }
};
}
