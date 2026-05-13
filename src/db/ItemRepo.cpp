#include "ItemRepo.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QStringList>
#include "../meta/MetadataDefs.h"
#include "../meta/MetadataManager.h"

namespace ArcMeta {

bool ItemRepo::save(const std::wstring& parentPath, const std::wstring& name, const ItemMeta& meta) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    std::wstring fullPath = parentPath;
    if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L'\\';
    fullPath += name;
    
    std::wstring volume = meta.volume;
    std::wstring frn = meta.frn;
    if (volume.empty() || frn.empty()) {
        // 2026-06-xx 物理重构：彻底杜绝 MD5 与空 FRN 混淆。
        // 若元数据未携带卷标/FRN，首先尝试从数据库查询既有身份。
        QSqlQuery check(db);
        check.prepare("SELECT volume, frn FROM items WHERE path = ?");
        check.addBindValue(QString::fromStdWString(fullPath));
        if (check.exec() && check.next()) {
            if (volume.empty()) volume = check.value(0).toString().toStdWString();
            if (frn.empty()) frn = check.value(1).toString().toStdWString();
        }
    }
    
    // 2026-06-xx 物理加固：如果依然缺失，强制调用 MetadataManager 提取物理或确定性伪指纹。
    // 严禁使用 "VIRTUAL" 硬编码，严禁使用 MD5。
    if (volume.empty()) volume = MetadataManager::getVolumeSerialNumber(fullPath);
    if (frn.empty()) {
        std::string dummyFid;
        MetadataManager::fetchWinApiMetadataDirect(fullPath, dummyFid, &frn);
    }

    // 2026-06-xx 借鉴“旧版本-2”：废除 REPLACE INTO，改用 INSERT OR IGNORE + UPDATE。
    // 铁律：必须保护 ctime、mtime、deleted 等核心字段不被重置。
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO items (volume, frn, path, parent_path, type) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(volume));
    q.addBindValue(QString::fromStdWString(frn));
    q.addBindValue(QString::fromStdWString(fullPath));
    q.addBindValue(QString::fromStdWString(parentPath));
    q.addBindValue(QString::fromStdWString(meta.type));
    q.exec();

    QSqlQuery u(db);
    // 2026-06-15 按照审计建议：动态绑定 type 字段，杜绝文件夹被污染为文件。
    // 铁律：元数据更新即视为激活该物理文件。
    u.prepare("UPDATE items SET rating = ?, color = ?, tags = ?, pinned = ?, note = ?, "
              "encrypted = ?, encrypt_salt = ?, encrypt_iv = ?, encrypt_verify_hash = ?, "
              "original_name = ?, file_id_128 = ?, size = ?, deleted = 0, type = ? "
              "WHERE volume = ? AND frn = ?");
    u.addBindValue(meta.rating);
    u.addBindValue(QString::fromStdWString(meta.color));
    
    QJsonArray tagsArr; 
    for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    u.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));
    
    u.addBindValue(meta.pinned ? 1 : 0);
    u.addBindValue(QString::fromStdWString(meta.note));
    u.addBindValue(meta.encrypted ? 1 : 0);
    u.addBindValue(QString::fromStdString(meta.encryptSalt));
    u.addBindValue(QString::fromStdString(meta.encryptIv));
    u.addBindValue(QString::fromStdString(meta.encryptVerifyHash));
    u.addBindValue(QString::fromStdWString(meta.originalName));
    u.addBindValue(QString::fromStdString(meta.fileId128));
    u.addBindValue(meta.size);
    u.addBindValue(QString::fromStdWString(meta.type)); // 物理对齐 type
    u.addBindValue(QString::fromStdWString(volume));
    u.addBindValue(QString::fromStdWString(frn));

    // 2026-06-15 物理迁移：处理从 Fallback ID (FRN标识) 到正式 128-bit 哈希的迁移
    // 解决上下文冲突：确保 category_items 中的关联关系同步更新，杜绝主键重复或数据爆炸
    QString oldFid;
    QSqlQuery check(db);
    check.prepare("SELECT file_id_128 FROM items WHERE volume = ? AND frn = ?");
    check.addBindValue(QString::fromStdWString(volume));
    check.addBindValue(QString::fromStdWString(frn));
    if (check.exec() && check.next()) oldFid = check.value(0).toString();

    QString newFid = QString::fromStdString(meta.fileId128);
    if (newFid.isEmpty() || newFid.startsWith("PATHURL:")) {
        // 2026-06-xx 物理修复：彻底杜绝 FRN 混淆。
        // 调用中心化的强标识算法，确保入库 ID 稳健。
        newFid = QString::fromStdString(MetadataManager::instance().getFileIdSync(fullPath));
    }

    if (!oldFid.isEmpty() && oldFid != newFid && oldFid.contains(":")) {
        QSqlQuery migrate(db);
        migrate.prepare("UPDATE category_items SET file_id_128 = ? WHERE file_id_128 = ?");
        migrate.addBindValue(newFid);
        migrate.addBindValue(oldFid);
        migrate.exec();
    }
    
    // 按照审计意见：移除冗余的 uFid 写入，合并进主事务（见上方的 UPDATE 语句已包含 file_id_128 绑定）
    return u.exec();
}

bool ItemRepo::saveBasicInfo(const std::wstring& volume, const std::wstring& frn, const std::wstring& path, const std::wstring& parentPath, bool isDir, qint64 mtime, qint64 size, qint64 ctime, const std::string& fileId128) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    
    // 2026-06-15 物理修复：统一 File ID 协议。
    QString finalFid = QString::fromStdString(fileId128);
    if (finalFid.isEmpty() || finalFid.startsWith("PATHURL:")) {
        finalFid = QString::fromStdString(MetadataManager::instance().getFileIdSync(path));
    }

    q.prepare("INSERT OR IGNORE INTO items (volume, frn, path, parent_path, type, file_id_128, ctime) VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(volume)); q.addBindValue(QString::fromStdWString(frn));
    q.addBindValue(QString::fromStdWString(path)); q.addBindValue(QString::fromStdWString(parentPath));
    q.addBindValue(isDir ? "folder" : "file"); q.addBindValue(finalFid); q.addBindValue(ctime); q.exec();
    
    QSqlQuery u(db);
    // 2026-06-15 按照审计意见：补全 size 字段写入，修正 COALESCE 逻辑以允许从临时 ID 升级到物理 ID。
    // 策略：如果当前 ID 包含冒号（临时态），则允许被新 ID 覆盖。
    u.prepare("UPDATE items SET path = ?, parent_path = ?, type = ?, mtime = ?, ctime = ?, size = ?, deleted = 0, "
              "file_id_128 = CASE WHEN file_id_128 LIKE '%:%' OR file_id_128 = '' THEN ? ELSE file_id_128 END "
              "WHERE volume = ? AND frn = ?");
    u.addBindValue(QString::fromStdWString(path)); u.addBindValue(QString::fromStdWString(parentPath));
    u.addBindValue(isDir ? "folder" : "file"); u.addBindValue(mtime); u.addBindValue(ctime); u.addBindValue(size);
    u.addBindValue(finalFid);
    u.addBindValue(QString::fromStdWString(volume)); u.addBindValue(QString::fromStdWString(frn));
    return u.exec();
}

bool ItemRepo::markAsDeleted(const std::wstring& volume, const std::wstring& frn) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db); q.prepare("UPDATE items SET deleted = 1 WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume)); q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

bool ItemRepo::removeByFrn(const std::wstring& volume, const std::wstring& frn) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db); q.prepare("DELETE FROM items WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume)); q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

std::wstring ItemRepo::getPathByFrn(const std::wstring& volume, const std::wstring& frn) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db); q.prepare("SELECT path FROM items WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume)); q.addBindValue(QString::fromStdWString(frn));
    if (q.exec() && q.next()) return q.value(0).toString().toStdWString();
    return L"";
}

bool ItemRepo::updatePath(const std::wstring& volume, const std::wstring& frn, const std::wstring& newPath, const std::wstring& newParentPath) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db); q.prepare("UPDATE items SET path = ?, parent_path = ? WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(newPath)); q.addBindValue(QString::fromStdWString(newParentPath));
    q.addBindValue(QString::fromStdWString(volume)); q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

QStringList ItemRepo::searchByKeyword(const QString& keyword, const QString& parentPath) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    
    // 2026-06-xx 物理修复：基于非空 Fallback ID 机制回归高性能查询。
    // 铁律：允许关键词为空。由于 ID 保证非空唯一，直接按 file_id_128 分组。
    QString sql;
    if (parentPath.isEmpty()) {
        if (keyword.isEmpty()) {
            sql = "SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' GROUP BY file_id_128 LIMIT 1000";
            q.prepare(sql);
        } else {
            sql = "SELECT MIN(path) FROM items WHERE path LIKE ? AND deleted = 0 AND type = 'file' GROUP BY file_id_128 LIMIT 1000";
            q.prepare(sql);
            q.addBindValue("%" + keyword + "%");
        }
    } else {
        if (keyword.isEmpty()) {
            sql = "SELECT MIN(path) FROM items WHERE parent_path = ? AND deleted = 0 AND type = 'file' GROUP BY file_id_128 LIMIT 1000";
            q.prepare(sql);
            q.addBindValue(parentPath);
        } else {
            sql = "SELECT MIN(path) FROM items WHERE parent_path = ? AND path LIKE ? AND deleted = 0 AND type = 'file' GROUP BY file_id_128 LIMIT 1000";
            q.prepare(sql);
            q.addBindValue(parentPath);
            q.addBindValue("%" + keyword + "%");
        }
    }
    
    QStringList results;
    if (q.exec()) {
        while (q.next()) results << q.value(0).toString();
    }
    return results;
}

QStringList ItemRepo::getUncategorizedPaths() {
    return getPathsBySystemType("uncategorized");
}

QStringList ItemRepo::getUntaggedPaths() {
    return getPathsBySystemType("untagged");
}

QStringList ItemRepo::getPathsBySystemType(const QString& type) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    
    // 2026-06-15 按照审计意见：时间戳从 double 迁移至 qint64 以确保毫秒精度。
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 startOfToday = QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    qint64 startOfYesterday = QDateTime(QDate::currentDate().addDays(-1), QTime(0, 0)).toMSecsSinceEpoch();

    // 2026-06-15 物理修复：路径列表获取逻辑基于非空 Fallback ID 机制回归。
    // 铁律：物理锁定 type='file'。
    if (type == "all") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' GROUP BY file_id_128 LIMIT 1000");
    } else if (type == "today") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' AND (ctime >= ? OR mtime >= ?) GROUP BY file_id_128 LIMIT 1000");
        q.addBindValue(startOfToday);
        q.addBindValue(startOfToday);
    } else if (type == "yesterday") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' AND (ctime >= ? OR mtime >= ?) AND (ctime < ? OR mtime < ?) GROUP BY file_id_128 LIMIT 1000");
        q.addBindValue(startOfYesterday);
        q.addBindValue(startOfYesterday);
        q.addBindValue(startOfToday);
        q.addBindValue(startOfToday);
    } else if (type == "recently_visited") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' AND atime >= ? GROUP BY file_id_128 LIMIT 1000");
        q.addBindValue(now - 86400000.0);
    } else if (type == "uncategorized") {
        q.prepare("SELECT MIN(i.path) FROM items i "
                  "WHERE i.deleted = 0 AND i.type = 'file' "
                  "AND NOT EXISTS ("
                  "  SELECT 1 FROM category_items ci "
                  "  WHERE ci.file_id_128 = i.file_id_128"
                  ") GROUP BY i.file_id_128 LIMIT 1000");
    } else if (type == "untagged") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 0 AND type = 'file' "
                  "AND (tags IS NULL OR tags = '' OR tags = '[]') GROUP BY file_id_128 LIMIT 1000");
    } else if (type == "trash") {
        q.prepare("SELECT MIN(path) FROM items WHERE deleted = 1 AND type = 'file' GROUP BY file_id_128 LIMIT 1000");
    } else {
        return {};
    }

    QStringList results;
    if (q.exec()) {
        while (q.next()) results << q.value(0).toString();
    }
    return results;
}

} // namespace ArcMeta
