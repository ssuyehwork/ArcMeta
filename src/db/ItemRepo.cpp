#include "ItemRepo.h"
#include "Database.h"
#include "CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QSqlDatabase>

namespace ArcMeta {
static std::wstring getVol(const std::wstring& path) { return MetadataManager::getVolumeSerialNumber(path); }
bool ItemRepo::save(const std::wstring& parentPath, const std::wstring& name, const ItemMeta& meta) {
    std::wstring fullPath = parentPath;
    if (!fullPath.empty() && (fullPath.back() != L'\\' && fullPath.back() != L'/')) fullPath += L'\\';
    fullPath += name;
    std::wstring vol = meta.volume.empty() ? getVol(fullPath) : meta.volume;
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO files (file_id_128, frn, path, parent_path, type, rating, color, tags, pinned, note, url, size, ctime, mtime, atime, deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
    q.addBindValue(QString::fromStdString(meta.fileId128)); q.addBindValue(QString::fromStdWString(meta.frn));
    q.addBindValue(QString::fromStdWString(fullPath)); q.addBindValue(QString::fromStdWString(parentPath));
    q.addBindValue(QString::fromStdWString(meta.type)); q.addBindValue(meta.rating);
    q.addBindValue(QString::fromStdWString(meta.color));
    QJsonArray tagsArr; for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));
    q.addBindValue(meta.pinned ? 1 : 0); q.addBindValue(QString::fromStdWString(meta.note));
    q.addBindValue(QString::fromStdWString(meta.url)); q.addBindValue(meta.size);
    q.addBindValue((double)meta.creationTime); q.addBindValue((double)meta.modificationTime); q.addBindValue((double)meta.accessTime);
    bool ok = q.exec();
    if (ok) {
        QSqlQuery dq(db); dq.prepare("DELETE FROM palettes WHERE file_id_128 = ?"); dq.addBindValue(QString::fromStdString(meta.fileId128)); dq.exec();
        for (const auto& p : meta.palettes) {
            QSqlQuery iq(db); iq.prepare("INSERT INTO palettes (file_id_128, r, g, b, ratio) VALUES (?, ?, ?, ?, ?)");
            iq.addBindValue(QString::fromStdString(meta.fileId128)); iq.addBindValue(p.color.red());
            iq.addBindValue(p.color.green()); iq.addBindValue(p.color.blue()); iq.addBindValue(p.ratio); iq.exec();
        }
        Database::instance().markDirty(vol);
    }
    return ok;
}
bool ItemRepo::saveBasicInfo(const std::wstring& volume, const std::wstring& frn, const std::wstring& path, const std::wstring& parentPath, bool isDir, qint64 mtime, qint64 size, qint64 ctime, const std::string& fileId128) {
    QSqlDatabase db = Database::instance().getThreadDatabase(volume);
    if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("INSERT OR REPLACE INTO files (file_id_128, frn, path, parent_path, type, mtime, size, ctime, deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)");
    q.addBindValue(QString::fromStdString(fileId128)); q.addBindValue(QString::fromStdWString(frn));
    q.addBindValue(QString::fromStdWString(path)); q.addBindValue(QString::fromStdWString(parentPath));
    q.addBindValue(isDir ? "folder" : "file"); q.addBindValue((double)mtime); q.addBindValue(size); q.addBindValue((double)ctime);
    bool ok = q.exec(); if (ok) Database::instance().markDirty(volume); return ok;
}
bool ItemRepo::removeByFrn(const std::wstring& vol, const std::wstring& frn) {
    QSqlDatabase db = Database::instance().getThreadDatabase(vol); if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("DELETE FROM files WHERE frn = ?"); q.addBindValue(QString::fromStdWString(frn));
    bool ok = q.exec(); if (ok) Database::instance().markDirty(vol); return ok;
}
bool ItemRepo::physicalRemove(const std::wstring& path) {
    std::wstring vol = getVol(path); QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("DELETE FROM files WHERE path = ? OR path LIKE ?");
    q.addBindValue(QString::fromStdWString(path)); q.addBindValue(QString::fromStdWString(path + L"\\%"));
    bool ok = q.exec(); if (ok) Database::instance().markDirty(vol); return ok;
}
bool ItemRepo::restoreByPath(const std::wstring& path) {
    std::wstring vol = getVol(path); QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("UPDATE files SET deleted = 0 WHERE path = ? OR path LIKE ?");
    q.addBindValue(QString::fromStdWString(path)); q.addBindValue(QString::fromStdWString(path + L"\\%"));
    bool ok = q.exec(); if (ok) Database::instance().markDirty(vol); return ok;
}
bool ItemRepo::markAsDeleted(const std::wstring& vol, const std::wstring& frn) {
    QSqlDatabase db = Database::instance().getThreadDatabase(vol); if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("UPDATE files SET deleted = 1 WHERE frn = ?"); q.addBindValue(QString::fromStdWString(frn));
    bool ok = q.exec(); if (ok) Database::instance().markDirty(vol); return ok;
}
std::wstring ItemRepo::getPathByFrn(const std::wstring& vol, const std::wstring& frn) {
    QSqlDatabase db = Database::instance().getThreadDatabase(vol); if (!db.isOpen()) return L"";
    QSqlQuery q(db); q.prepare("SELECT path FROM files WHERE frn = ?"); q.addBindValue(QString::fromStdWString(frn));
    if (q.exec() && q.next()) return q.value(0).toString().toStdWString(); return L"";
}
bool ItemRepo::updatePath(const std::wstring& vol, const std::wstring& frn, const std::wstring& newP, const std::wstring& newPp) {
    QSqlDatabase db = Database::instance().getThreadDatabase(vol); if (!db.isOpen()) return false;
    QSqlQuery q(db); q.prepare("UPDATE files SET path = ?, parent_path = ? WHERE frn = ?");
    q.addBindValue(QString::fromStdWString(newP)); q.addBindValue(QString::fromStdWString(newPp)); q.addBindValue(QString::fromStdWString(frn));
    bool ok = q.exec(); if (ok) Database::instance().markDirty(vol); return ok;
}
QStringList ItemRepo::searchByKeyword(const QString& k, const QString& p) {
    QStringList res; auto vols = Database::instance().getMountedVolumes();
    for (const auto& v : vols) {
        QSqlDatabase db = Database::instance().getThreadDatabase(v); if (!db.isOpen()) continue;
        QSqlQuery q(db); QString sql = "SELECT path FROM files WHERE path LIKE ? AND deleted = 0";
        if (!p.isEmpty()) sql += " AND parent_path = ?";
        q.prepare(sql); q.addBindValue("%" + k + "%"); if (!p.isEmpty()) q.addBindValue(p);
        if (q.exec()) while (q.next()) res << q.value(0).toString();
    }
    return res;
}
QStringList ItemRepo::getPathsBySystemType(const QString& t) {
    QStringList res; auto vols = Database::instance().getMountedVolumes();
    for (const auto& v : vols) {
        QSqlDatabase db = Database::instance().getThreadDatabase(v); if (!db.isOpen()) continue;
        QSqlQuery q(db);
        if (t == "all") q.prepare("SELECT path FROM files WHERE deleted = 0 AND type = 'file'");
        else if (t == "trash") q.prepare("SELECT path FROM files WHERE deleted = 1");
        else continue;
        if (q.exec()) while (q.next()) res << q.value(0).toString();
    }
    return res;
}
std::vector<ItemRepo::ItemRecord> ItemRepo::getItemRecordsBySystemType(const QString&) { return {}; }
std::vector<ItemRepo::ItemRecord> ItemRepo::searchRecordsByKeyword(const QString&, const QString&) { return {}; }
std::vector<ItemRepo::ItemRecord> ItemRepo::getRecordsInCategory(int id) {
    std::vector<ItemRecord> res; auto items = CategoryRepo::getFileIdsInCategory(id);
    for (const auto& it : items) {
        QSqlDatabase db = Database::instance().getThreadDatabase(it.second); if (!db.isOpen()) continue;
        QSqlQuery q(db); q.prepare("SELECT path, type FROM files WHERE file_id_128 = ?");
        q.addBindValue(QString::fromStdString(it.first));
        if (q.exec() && q.next()) { ItemRecord r; r.path = q.value(0).toString(); r.isDir = q.value(1).toString() == "folder"; r.volume = QString::fromStdWString(it.second); res.push_back(r); }
    }
    return res;
}
}
