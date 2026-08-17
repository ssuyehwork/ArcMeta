#include "DiskTrashService.h"
#include "../meta/DatabaseManager.h"
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

bool DiskTrashService::moveToDiskTrash(const QStringList& paths) {
    if (paths.isEmpty()) return true;

    bool allOk = true;
    for (const QString& p : paths) {
        QFileInfo info(p);
        QString drive = info.absolutePath().left(3); // e.g. "C:/"
        QString trashDir = drive + ".arcmeta/disk_trash";
        QDir().mkpath(trashDir);

#ifdef Q_OS_WIN
        // 确保 .arcmeta 目录隐藏
        SetFileAttributesW((drive + ".arcmeta").toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

        QString dest = trashDir + "/" + info.fileName();
        // 冲突处理：如果回收站已有同名文件，增加时间戳后缀
        if (QFile::exists(dest)) {
            dest = trashDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + info.fileName();
        }

        // 1. 物理同盘位移 (秒级移动)
        if (QFile::rename(p, dest)) {
            // 2. 写入独立的 disk_trash 数据库，不污染 metadata 表
            sqlite3* db = DatabaseManager::instance().getDbForPath(p.toStdWString());
            if (!db) {
                allOk = false;
                continue;
            }

            SqlTransaction trans(db);
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "INSERT INTO disk_trash (trash_path, original_path, drive_letter, file_name, is_folder, file_size, deleted_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
            
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                QString driveLetter = drive.left(1).toUpper();
                sqlite3_bind_text16(stmt, 1, dest.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 2, p.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 3, driveLetter.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text16(stmt, 4, info.fileName().toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 5, info.isDir() ? 1 : 0);
                sqlite3_bind_int64(stmt, 6, info.size());
                sqlite3_bind_int64(stmt, 7, QDateTime::currentMSecsSinceEpoch());

                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    trans.commit();
                    DatabaseManager::instance().setDirty(true);
                } else {
                    qWarning() << "[DiskTrashService] Failed to insert disk_trash row:" << sqlite3_errmsg(db);
                    allOk = false;
                }
                sqlite3_finalize(stmt);
            } else {
                qWarning() << "[DiskTrashService] Failed to prepare insert stmt:" << sqlite3_errmsg(db);
                allOk = false;
            }
        } else {
            qWarning() << "[DiskTrashService] Physical move to trash failed for path:" << p;
            allOk = false;
        }
    }
    return allOk;
}

bool DiskTrashService::restoreFromDiskTrash(int id, const QString& trashPath) {
    sqlite3* db = DatabaseManager::instance().getDbForPath(trashPath.toStdWString());
    if (!db) return false;

    QString originalPath;
    sqlite3_stmt* stmt = nullptr;
    const char* sqlSel = "SELECT original_path FROM disk_trash WHERE id = ?";
    if (sqlite3_prepare_v2(db, sqlSel, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const wchar_t* wOrig = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 0));
            if (wOrig) {
                originalPath = QString::fromWCharArray(wOrig);
            }
        }
        sqlite3_finalize(stmt);
    }

    if (originalPath.isEmpty()) {
        qWarning() << "[DiskTrashService] Failed to find original path for trash id:" << id;
        return false;
    }

    // 自动创建目标父目录
    QDir().mkpath(QFileInfo(originalPath).absolutePath());

    // QFile::rename 移回 original_path
    if (QFile::rename(trashPath, originalPath)) {
        SqlTransaction trans(db);
        sqlite3_stmt* delStmt = nullptr;
        const char* sqlDel = "DELETE FROM disk_trash WHERE id = ?";
        bool success = false;
        if (sqlite3_prepare_v2(db, sqlDel, -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, id);
            if (sqlite3_step(delStmt) == SQLITE_DONE) {
                trans.commit();
                DatabaseManager::instance().setDirty(true);
                success = true;
            }
            sqlite3_finalize(delStmt);
        }
        if (success) return true;
    } else {
        qWarning() << "[DiskTrashService] Failed to physically move back trash item to original path:" << originalPath;
    }

    return false;
}

bool DiskTrashService::permanentlyDeleteDiskTrash(int id, const QString& trashPath) {
    sqlite3* db = DatabaseManager::instance().getDbForPath(trashPath.toStdWString());
    if (!db) return false;

    // 物理彻底删除 (如果是文件夹则递归删除)
    QFileInfo info(trashPath);
    bool physicalOk = false;
    if (info.exists()) {
        if (info.isDir()) {
            physicalOk = QDir(trashPath).removeRecursively();
        } else {
            physicalOk = QFile::remove(trashPath);
        }
    } else {
        physicalOk = true; // 文件在物理上已经不存在，直接认为成功，允许清洗数据库
    }

    if (physicalOk) {
        SqlTransaction trans(db);
        sqlite3_stmt* delStmt = nullptr;
        const char* sqlDel = "DELETE FROM disk_trash WHERE id = ?";
        bool success = false;
        if (sqlite3_prepare_v2(db, sqlDel, -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, id);
            if (sqlite3_step(delStmt) == SQLITE_DONE) {
                trans.commit();
                DatabaseManager::instance().setDirty(true);
                success = true;
            }
            sqlite3_finalize(delStmt);
        }
        if (success) return true;
    }

    return false;
}

bool DiskTrashService::restoreAllDiskTrash() {
    bool allOk = true;
    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs();
    
    for (sqlite3* db : dbs) {
        struct TrashItem {
            int id;
            QString trashPath;
        };
        std::vector<TrashItem> items;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, trash_path FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                const wchar_t* wPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                if (wPath) {
                    items.push_back({id, QString::fromWCharArray(wPath)});
                }
            }
            sqlite3_finalize(stmt);
        }

        for (const auto& item : items) {
            if (!restoreFromDiskTrash(item.id, item.trashPath)) {
                allOk = false;
            }
        }
    }
    return allOk;
}

bool DiskTrashService::emptyDiskTrash() {
    bool allOk = true;
    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs();

    for (sqlite3* db : dbs) {
        struct TrashItem {
            int id;
            QString trashPath;
        };
        std::vector<TrashItem> items;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, trash_path FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                const wchar_t* wPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                if (wPath) {
                    items.push_back({id, QString::fromWCharArray(wPath)});
                }
            }
            sqlite3_finalize(stmt);
        }

        for (const auto& item : items) {
            if (!permanentlyDeleteDiskTrash(item.id, item.trashPath)) {
                allOk = false;
            }
        }
    }
    return allOk;
}

} // namespace ArcMeta
