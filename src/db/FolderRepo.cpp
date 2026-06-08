#include "FolderRepo.h"
#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace ArcMeta {

bool FolderRepo::save(const std::wstring& volume, const std::wstring& path, const FolderMeta& meta) {
    QSqlDatabase db = Database::instance().getThreadDatabase(volume);
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    
    q.prepare("INSERT OR REPLACE INTO files (file_id_128, path, parent_path, type, rating, color, tags, pinned, note, url, deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
    q.addBindValue(QString::fromStdString(meta.fileId128));
    q.addBindValue(QString::fromStdWString(path));

    QFileInfo fi(QString::fromStdWString(path));
    q.addBindValue(QString::fromStdWString(fi.absolutePath().toStdWString()));
    q.addBindValue("folder");
    q.addBindValue(meta.rating);
    q.addBindValue(QString::fromStdWString(meta.color));
    
    QJsonArray tagsArr;
    for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));
    
    q.addBindValue(meta.pinned ? 1 : 0);
    q.addBindValue(QString::fromStdWString(meta.note));
    q.addBindValue(QString::fromStdWString(meta.url));
    
    bool ok = q.exec();
    if (ok) Database::instance().markDirty(volume);
    return ok;
}

bool FolderRepo::get(const std::wstring& volume, const std::wstring& path, FolderMeta& meta) {
    QSqlDatabase db = Database::instance().getThreadDatabase(volume);
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("SELECT file_id_128, rating, color, tags, pinned, note, url FROM files WHERE path = ? AND type = 'folder'");
    q.addBindValue(QString::fromStdWString(path));
    
    if (q.exec() && q.next()) {
        meta.fileId128 = q.value(0).toString().toStdString();
        meta.rating = q.value(1).toInt();
        meta.color = q.value(2).toString().toStdWString();
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toByteArray());
        meta.tags.clear();
        if (doc.isArray()) {
            for (const auto& v : doc.array()) meta.tags.push_back(v.toString().toStdWString());
        }

        meta.pinned = q.value(4).toInt() != 0;
        meta.note = q.value(5).toString().toStdWString();
        meta.url = q.value(6).toString().toStdWString();
        return true;
    }
    return false;
}

bool FolderRepo::remove(const std::wstring& volume, const std::wstring& path) {
    QSqlDatabase db = Database::instance().getThreadDatabase(volume);
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("DELETE FROM files WHERE path = ? AND type = 'folder'");
    q.addBindValue(QString::fromStdWString(path));
    bool ok = q.exec();
    if (ok) Database::instance().markDirty(volume);
    return ok;
}

} // namespace ArcMeta
