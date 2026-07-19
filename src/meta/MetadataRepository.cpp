#include "MetadataRepository.h"
#include <QStringList>
#include <QDebug>

namespace ArcMeta {

bool MetadataRepository::saveMeta(sqlite3* db, const std::wstring& path, const RuntimeMeta& meta) {
    if (!db) return false;

    const char* sql = R"(
        INSERT INTO metadata (
            file_id, path, is_folder, rating, color, tags, note, url, 
            ctime, mtime, atime, file_size, palettes, is_trash, original_path, 
            is_invalid, width, height, ingestion_status
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(file_id) DO UPDATE SET
            path = excluded.path,
            is_folder = excluded.is_folder,
            rating = excluded.rating,
            color = excluded.color,
            tags = excluded.tags,
            note = excluded.note,
            url = excluded.url,
            ctime = excluded.ctime,
            mtime = excluded.mtime,
            atime = excluded.atime,
            file_size = excluded.file_size,
            palettes = excluded.palettes,
            is_trash = excluded.is_trash,
            original_path = excluded.original_path,
            is_invalid = excluded.is_invalid,
            width = excluded.width,
            height = excluded.height,
            ingestion_status = excluded.ingestion_status;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        qWarning() << "[Repo] saveMeta prepare fail:" << sqlite3_errmsg(db);
        return false;
    }

    std::string pathUtf8 = QString::fromStdWString(path).toUtf8().toStdString();
    std::string colorUtf8 = QString::fromStdWString(meta.color).toUtf8().toStdString();
    std::string tagsStr = meta.tags.join(",").toUtf8().toStdString();
    std::string noteUtf8 = QString::fromStdWString(meta.note).toUtf8().toStdString();
    std::string urlUtf8 = QString::fromStdWString(meta.url).toUtf8().toStdString();
    std::string originalPathUtf8 = QString::fromStdWString(meta.originalPath).toUtf8().toStdString();

    sqlite3_bind_text(stmt, 1, meta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, meta.isFolder ? 1 : 0);
    sqlite3_bind_int(stmt, 4, meta.rating);
    sqlite3_bind_text(stmt, 5, colorUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, noteUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, urlUtf8.c_str(), -1, SQLITE_TRANSIENT);
    
    sqlite3_bind_int64(stmt, 9, meta.ctime);
    sqlite3_bind_int64(stmt, 10, meta.mtime);
    sqlite3_bind_int64(stmt, 11, meta.atime);
    sqlite3_bind_int64(stmt, 12, meta.fileSize);

    // 绑定 palettes 二进制数据
    QByteArray pData;
    for (const auto& p : meta.palettes) {
        pData.append(p.color.name().toUtf8());
        pData.append(",");
        pData.append(QString::number(p.ratio).toUtf8());
        pData.append(";");
    }
    sqlite3_bind_blob(stmt, 13, pData.constData(), pData.size(), SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 14, meta.isTrash ? 1 : 0);
    sqlite3_bind_text(stmt, 15, originalPathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 16, meta.isInvalid ? 1 : 0);
    sqlite3_bind_int(stmt, 17, meta.width);
    sqlite3_bind_int(stmt, 18, meta.height);
    sqlite3_bind_int(stmt, 19, meta.ingestionStatus);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool MetadataRepository::getMeta(sqlite3* db, const std::wstring& path, RuntimeMeta& outMeta) {
    if (!db) return false;

    const char* sql = "SELECT file_id, rating, color, tags, note, url, ctime, mtime, atime, file_size, palettes, is_trash, original_path, is_invalid, width, height, ingestion_status, is_folder FROM metadata WHERE path = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string pathUtf8 = QString::fromStdWString(path).toUtf8().toStdString();
    sqlite3_bind_text(stmt, 1, pathUtf8.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        outMeta.fileId128 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        outMeta.rating = sqlite3_column_int(stmt, 1);
        outMeta.color = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))).toStdWString();
        
        QString tagsStr = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        if (!tagsStr.isEmpty()) outMeta.tags = tagsStr.split(",");

        outMeta.note = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))).toStdWString();
        outMeta.url = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))).toStdWString();
        
        outMeta.ctime = sqlite3_column_int64(stmt, 6);
        outMeta.mtime = sqlite3_column_int64(stmt, 7);
        outMeta.atime = sqlite3_column_int64(stmt, 8);
        outMeta.fileSize = sqlite3_column_int64(stmt, 9);

        // 解析 palettes BLOB
        const char* blob = reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 10));
        int len = sqlite3_column_bytes(stmt, 10);
        if (blob && len > 0) {
            QString blobStr = QString::fromUtf8(blob, len);
            QStringList parts = blobStr.split(";");
            outMeta.palettes.clear();
            for (const auto& part : parts) {
                if (part.isEmpty()) continue;
                QStringList sub = part.split(",");
                if (sub.size() == 2) {
                    PaletteEntry pe;
                    pe.color = QColor(sub[0]);
                    pe.ratio = sub[1].toFloat();
                    outMeta.palettes.push_back(pe);
                }
            }
        }

        outMeta.isTrash = (sqlite3_column_int(stmt, 11) == 1);
        outMeta.originalPath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12))).toStdWString();
        outMeta.isInvalid = (sqlite3_column_int(stmt, 13) == 1);
        outMeta.width = sqlite3_column_int(stmt, 14);
        outMeta.height = sqlite3_column_int(stmt, 15);
        outMeta.ingestionStatus = sqlite3_column_int(stmt, 16);
        outMeta.isFolder = (sqlite3_column_int(stmt, 17) == 1);
    }
    sqlite3_finalize(stmt);
    return found;
}

bool MetadataRepository::deleteMeta(sqlite3* db, const std::wstring& path) {
    if (!db) return false;

    const char* sql = "DELETE FROM metadata WHERE path = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string pathUtf8 = QString::fromStdWString(path).toUtf8().toStdString();
    sqlite3_bind_text(stmt, 1, pathUtf8.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace ArcMeta
