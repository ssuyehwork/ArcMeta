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
        // 🚨 第一步：盘查并物理清理空托管包 (磁盘 -> 数据库)
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
        // 🚨 第二步：反查数据库死记录 (数据库 -> 磁盘)
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
                        bool exists = false;
                        if (QFileInfo(qPath).isDir()) {
                            exists = QDir(qPath).exists();
                        } else {
                            exists = QFile::exists(qPath);
                        }

                        if (!exists) {
                            allGhostFolderIds << QString::fromUtf8(fidText);
                            allGhostPaths << qPath;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        // 合并空包和幽灵文件的 folderIds & paths 进行强力物理+数据库级联删除
        QStringList targetsToRemovePaths = allEmptyArcDirs + allGhostPaths;
        QStringList targetsToRemoveFolderIds = allEmptyFolderIds + allGhostFolderIds;

        if (!targetsToRemovePaths.isEmpty()) {
            // 1. 先通过常规 removeMetadataBatchSync 进行内存缓存/索引同步清理及总计数调整
            MetadataManager::instance().removeMetadataBatchSync(targetsToRemovePaths);

            // 2. 数据库强力后备死角兜底：对所有可能未载入内存的幽灵数据进行纯 SQL 直接落盘删除
            for (sqlite3* db : dbs) {
                SqlTransaction trans(db);
                sqlite3_stmt* stmtMeta = nullptr;
                sqlite3_stmt* stmtItems = nullptr;
                sqlite3_stmt* stmtStats = nullptr;

                if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE folder_id = ?", -1, &stmtMeta, nullptr) == SQLITE_OK &&
                    sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmtItems, nullptr) == SQLITE_OK) {

                    for (const QString& fid : targetsToRemoveFolderIds) {
                        std::string stdFid = fid.toStdString();

                        // 从 metadata 删除
                        sqlite3_bind_text(stmtMeta, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtMeta);
                        sqlite3_reset(stmtMeta);

                        // 从 category_items 删除
                        sqlite3_bind_text(stmtItems, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtItems);
                        sqlite3_reset(stmtItems);
                    }
                }
                if (stmtMeta) sqlite3_finalize(stmtMeta);
                if (stmtItems) sqlite3_finalize(stmtItems);

                // 同时清理关联的 PROGRESS 进度记录
                for (const QString& qp : targetsToRemovePaths) {
                    std::string progressKey = "PROGRESS:" + qp.toUtf8().toStdString();
                    if (sqlite3_prepare_v2(db, "DELETE FROM system_stats WHERE key = ?", -1, &stmtStats, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(stmtStats, 1, progressKey.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtStats);
                        sqlite3_finalize(stmtStats);
                    }
                }
                trans.commit();
            }

            // 3. 物理彻底擦除磁盘空目录（仅针对第一步判定为空的包）
            for (const QString& path : allEmptyArcDirs) {
                QDir(path).removeRecursively();
            }

            cleanCount = allEmptyArcDirs.size();
            ghostCount = allGhostFolderIds.size();
        }

        // ==========================================
        // 🚨 第三步：清洗孤立关联 (category_items -> metadata)
        // ==========================================
        for (sqlite3* db : dbs) {
            SqlTransaction trans(db);
            char* errMsg = nullptr;
            const char* sqlCleanOrphans = "DELETE FROM category_items WHERE folder_id NOT IN (SELECT folder_id FROM metadata)";
            int rc = sqlite3_exec(db, sqlCleanOrphans, nullptr, nullptr, &errMsg);
            if (rc == SQLITE_OK) {
                int affected = sqlite3_changes(db);
                if (affected > 0) {
                    orphanCount += affected;
                }
            } else {
                if (errMsg) {
                    qWarning() << "[Cleanup] Clean orphans error:" << errMsg;
                    sqlite3_free(errMsg);
                }
            }
            trans.commit();
        }

        if (cleanCount > 0 || ghostCount > 0 || orphanCount > 0) {
            CategoryRepo::s_countsDirty.store(true);
        }

        emit cleanFinished(cleanCount, ghostCount, orphanCount);
    });
}

} // namespace ArcMeta
