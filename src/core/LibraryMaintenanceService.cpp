#include "LibraryMaintenanceService.h" 
#include "../meta/DatabaseManager.h" 
#include "../meta/MetadataManager.h" 
#include "../meta/CategoryRepo.h" 
#include <QtConcurrent> 
#include <QDir> 
#include <QFileInfo> 
#include <QDebug> 
 
namespace ArcMeta { 
 
void LibraryMaintenanceService::scanAndCleanEmptyArcsAsync() { 
    (void)QtConcurrent::run([this]() { 
        int cleanCount = 0; 
        int ghostCount = 0; 
        int orphanCount = 0; 
 
        auto dbs = DatabaseManager::instance().getActiveMemoryDbs(); 
 
        // ========================================== 
        // 第一步：盘查并物理清理空托管包 (磁盘 -> 数据库) 
        // ========================================== 
        const auto drives = QDir::drives(); 
        QStringList allEmptyArcDirs; 
        QStringList allEmptyFolderIds; 
 
        for (const QFileInfo& drive : drives) { 
            QString letter = drive.absolutePath().left(1).toUpper(); 
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString()); 
            if (volSerial == L"UNKNOWN") continue; 
 
            std::wstring managedRootW = MetadataManager::getManagedLibraryPath(volSerial, letter); 
            if (managedRootW.empty()) continue; 
 
            QString managedRoot = QString::fromStdWString(managedRootW); 
            QDir libDir(managedRoot); 
            if (!libDir.exists()) continue; 
 
            QStringList arcEntries = libDir.entryList({"*.arc"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden); 
            for (const QString& arcName : arcEntries) { 
                QFileInfo arcInfo(libDir.absoluteFilePath(arcName)); 
                QString baseName = arcInfo.completeBaseName(); 
                if (baseName.length() != 13) continue; 
 
                QDir arcDir(arcInfo.absoluteFilePath()); 
                QStringList entries = arcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden); 
                bool hasRealMaterials = false; 
                for (const QString& fName : entries) { 
                    if (fName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue; 
                    if (fName.compare(".ArcMeta.json", Qt::CaseInsensitive) == 0) continue; 
                    hasRealMaterials = true; 
                    break; 
                } 
 
                if (!hasRealMaterials) { 
                    allEmptyArcDirs << arcInfo.absoluteFilePath(); 
                    allEmptyFolderIds << baseName; 
                } 
            } 
        } 
 
        // ========================================== 
        // 第二步：反查数据库死记录 (数据库 -> 磁盘) 
        // ========================================== 
        QStringList allGhostFolderIds; 
        QStringList allGhostPaths; 
 
        for (sqlite3* db : dbs) { 
            sqlite3_stmt* stmt = nullptr; 
            const char* sqlQuery = "SELECT folder_id, path FROM metadata"; 
            if (sqlite3_prepare_v2(db, sqlQuery, -1, &stmt, nullptr) == SQLITE_OK) { 
                while (sqlite3_step(stmt) == SQLITE_ROW) { 
                    const char* fidText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)); 
                    const wchar_t* pathText = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1)); 
                    if (fidText && pathText) { 
                        QString qPath = QString::fromStdWString(pathText); 
                        bool exists = QFileInfo(qPath).isDir() ? QDir(qPath).exists() : QFile::exists(qPath); 
                        if (!exists) { 
                            allGhostFolderIds << QString::fromUtf8(fidText); 
                            allGhostPaths << qPath; 
                        } 
                    } 
                } 
                sqlite3_finalize(stmt); 
            } 
        } 
 
        QStringList targetsToRemovePaths = allEmptyArcDirs + allGhostPaths; 
        QStringList targetsToRemoveFolderIds = allEmptyFolderIds + allGhostFolderIds; 
 
        if (!targetsToRemovePaths.isEmpty()) { 
            MetadataManager::instance().removeMetadataBatchSync(targetsToRemovePaths); 
 
            // 🚀 【事务安全】：强行使用 SqlTransaction 保护批量删除，杜绝锁死 
            for (sqlite3* db : dbs) { 
                SqlTransaction trans(db); 
                sqlite3_stmt* stmtMeta = nullptr; 
                sqlite3_stmt* stmtItems = nullptr; 
 
                if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE folder_id = ?", -1, &stmtMeta, nullptr) == SQLITE_OK && 
                    sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmtItems, nullptr) == SQLITE_OK) { 
                     
                    for (const QString& fid : targetsToRemoveFolderIds) { 
                        std::string stdFid = fid.toStdString(); 
                        sqlite3_bind_text(stmtMeta, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT); 
                        sqlite3_step(stmtMeta); 
                        sqlite3_reset(stmtMeta); 
 
                        sqlite3_bind_text(stmtItems, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT); 
                        sqlite3_step(stmtItems); 
                        sqlite3_reset(stmtItems); 
                    } 
                } 
                if (stmtMeta) sqlite3_finalize(stmtMeta); 
                if (stmtItems) sqlite3_finalize(stmtItems); 
                trans.commit(); 
            } 
 
            for (const QString& path : allEmptyArcDirs) { 
                QDir(path).removeRecursively(); 
            } 
 
            cleanCount = allEmptyArcDirs.size(); 
            ghostCount = allGhostFolderIds.size(); 
        } 
 
        // ========================================== 
        // 第三步：清洗孤立关联 (category_items -> metadata) 
        // ========================================== 
        for (sqlite3* db : dbs) { 
            SqlTransaction trans(db); 
            char* errMsg = nullptr; 
            const char* sqlCleanOrphans = "DELETE FROM category_items WHERE folder_id NOT IN (SELECT folder_id FROM metadata)"; 
            if (sqlite3_exec(db, sqlCleanOrphans, nullptr, nullptr, &errMsg) == SQLITE_OK) { 
                orphanCount += sqlite3_changes(db); 
            } else if (errMsg) { 
                sqlite3_free(errMsg); 
            } 
            trans.commit(); 
        } 
 
 
        emit cleanFinished(cleanCount, ghostCount, orphanCount); 
    }); 
} 
 
} // namespace ArcMeta 
