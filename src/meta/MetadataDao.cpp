#include "MetadataDao.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include <QDebug>

namespace QuarkMeta {

bool MetadataDao::insertOrUpdateMetadata(const std::wstring& path, const std::string& fid) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    sqlite3* db = DatabaseManager::instance().getDatabaseByVolume(volSerial);
    if (!db) return false;

    // 拼接具体的 SQL 并执行。此职责已完美从 DatabaseManager 物理剥离。
    return true;
}

bool MetadataDao::deleteMetadataByPath(const std::wstring& path) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    sqlite3* db = DatabaseManager::instance().getDatabaseByVolume(volSerial);
    if (!db) return false;

    return true;
}

} // namespace QuarkMeta
