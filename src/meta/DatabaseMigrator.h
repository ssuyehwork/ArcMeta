#pragma once
#include "sqlite3.h"
#include <string>
#include <QString>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

class DatabaseMigrator {
public:
    static bool ensureActivated(sqlite3* db) {
        // 专门负责 CREATE TABLE、ALTER TABLE 升级
        const char* sqlCreateMetadata = 
            "CREATE TABLE IF NOT EXISTS metadata ("
            "  folder_id TEXT PRIMARY KEY, "
            "  path TEXT UNIQUE, "
            "  rating INTEGER, "
            "  color TEXT, "
            "  pinned INTEGER"
            ");";
        return sqlite3_exec(db, sqlCreateMetadata, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    static void performDataCleanup(sqlite3* db) {
        // 彻底剥离出的 DELETE 清洗脚本，保持开库轻量级
        sqlite3_exec(db, "DELETE FROM categories WHERE id <= 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "DELETE FROM categories WHERE name LIKE '%.arc';", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "DELETE FROM category_items WHERE path_hint LIKE '%.arc' ESCAPE '\\' "
                         "OR path_hint LIKE '%.arc\\%' ESCAPE '\\';", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "DELETE FROM metadata WHERE path LIKE '%_thumbnail.png';", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "DELETE FROM category_items WHERE path_hint LIKE '%_thumbnail.png';", nullptr, nullptr, nullptr);
    }
};

class VolumePathResolver {
public:
    static std::wstring getVolumeSerialNumber(const std::wstring& path) {
        if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
#ifdef Q_OS_WIN
        wchar_t root[4] = { static_cast<wchar_t>(towupper(path[0])), L':', L'\\', L'\0' };
        wchar_t volumeName[MAX_PATH + 1] = { 0 };
        DWORD serialNumber = 0;
        if (GetVolumeInformationW(root, volumeName, MAX_PATH, &serialNumber, nullptr, nullptr, nullptr, 0)) {
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%08X", serialNumber);
            return std::wstring(buf);
        }
#endif
        return L"UNKNOWN";
    }
};

} // namespace ArcMeta
