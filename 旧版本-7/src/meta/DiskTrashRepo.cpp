#include "DiskTrashRepo.h" 
#include "DatabaseManager.h" 
#include "sqlite3.h" 
#include <QMutexLocker> 
 
namespace ArcMeta { 
 
std::vector<DiskTrashRawItem> DiskTrashRepo::getAllTrashItems() { 
    std::vector<DiskTrashRawItem> results; 
 
    // 🚨 核心修复：使用 DatabaseManager 的全局互斥锁，彻底避免并发句柄破坏 
    QMutexLocker locker(DatabaseManager::instance().dbMutex()); 
 
    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs(); 
    for (sqlite3* db : dbs) { 
        sqlite3_stmt* stmt = nullptr; 
        const char* sql = "SELECT id, trash_path, original_path, drive_letter, file_name, is_folder, file_size, deleted_at FROM disk_trash"; 
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) { 
            while (sqlite3_step(stmt) == SQLITE_ROW) { 
                DiskTrashRawItem r; 
                r.id = sqlite3_column_int(stmt, 0); 
                const wchar_t* wTrashPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1)); 
                const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2)); 
                const wchar_t* wFileName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4)); 
                r.isFolder = (sqlite3_column_int(stmt, 5) != 0); 
                r.fileSize = sqlite3_column_int64(stmt, 6); 
                r.deletedAt = sqlite3_column_int64(stmt, 7); 
 
                if (wTrashPath && wOrigPath) { 
                    r.trashPath = wTrashPath; 
                    r.originalPath = wOrigPath; 
                    r.fileName = wFileName ? wFileName : L""; 
                    results.push_back(r); 
                } 
            } 
            sqlite3_finalize(stmt); 
        } 
    } 
    return results; 
} 
 
} 
