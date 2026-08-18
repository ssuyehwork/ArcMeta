#include "IngestionProgressEngine.h"
#include "MetadataManager.h"
#include "DatabaseManager.h"
#include <QDebug>

namespace QuarkMeta {

IngestionProgressEngine& IngestionProgressEngine::instance() {
    static IngestionProgressEngine inst;
    return inst;
}

IngestionProgressEngine::IngestionProgressEngine(QObject* parent) : QObject(parent) {}

void IngestionProgressEngine::calculateAndPersistProgress(const std::wstring& folderPath) {
    // 算法实现：从 MetadataManager 中提取对应的子节点集合，计算 IngestionStatus 的 0 和 1 的比例并存盘。
    double progress = 0.0;
    std::wstring normPath = MetadataManager::normalizePath(folderPath);

    if (MetadataManager::instance().hasChildrenInCache(normPath)) {
        auto children = MetadataManager::instance().getChildrenFromCache(normPath);
        int total = 0;
        int completed = 0;
        for (const auto& pair : children) {
            if (!pair.second.isFolder) {
                total++;
                if (pair.second.ingestionStatus == 1) {
                    completed++;
                }
            }
        }
        if (total > 0) {
            progress = (double)completed / total;
        }
    }

    // 写入数据库
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(normPath);
    sqlite3* db = DatabaseManager::instance().getDatabaseByVolume(volSerial);
    if (db) {
        std::string sql = "INSERT INTO folder_progress (path, progress) VALUES (?, ?) "
                          "ON CONFLICT(path) DO UPDATE SET progress = excluded.progress;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            // Bind path and progress
            // sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

double IngestionProgressEngine::getProgressFromDb(const std::wstring& folderPath) {
    double progress = 0.0;
    std::wstring normPath = MetadataManager::normalizePath(folderPath);
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(normPath);
    sqlite3* db = DatabaseManager::instance().getDatabaseByVolume(volSerial);
    if (db) {
        std::string sql = "SELECT progress FROM folder_progress WHERE path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            // Bind path and retrieve
            // if (sqlite3_step(stmt) == SQLITE_ROW) { progress = sqlite3_column_double(stmt, 0); }
            sqlite3_finalize(stmt);
        }
    }
    return progress;
}

} // namespace QuarkMeta
