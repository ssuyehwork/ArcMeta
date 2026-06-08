#pragma once
#include <QString>
#include <QCoreApplication>
#include <QDir>
#include <windows.h>
#include <string>

namespace ArcMeta {

class PathManager {
public:
    static std::wstring getAppDataDir() {
        QString path = QCoreApplication::applicationDirPath() + "/.arcmeta";
        std::wstring wPath = QDir::toNativeSeparators(path).toStdWString();

        QDir dir(path);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        SetFileAttributesW(wPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
        return wPath;
    }

    static std::wstring getGlobalDbPath() {
        return getAppDataDir() + L"\\global.db";
    }

    static std::wstring getVolumeDbPath(const std::wstring& volSerial) {
        return getAppDataDir() + L"\\vol_" + volSerial + L".db";
    }

    static void hideFile(const std::wstring& filePath) {
        SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
        // 尝试隐藏可能存在的临时文件
        SetFileAttributesW((filePath + L"-wal").c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetFileAttributesW((filePath + L"-shm").c_str(), FILE_ATTRIBUTE_HIDDEN);
        SetFileAttributesW((filePath + L"-journal").c_str(), FILE_ATTRIBUTE_HIDDEN);
    }
};

} // namespace ArcMeta
