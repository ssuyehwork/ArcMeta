#include "FavoritesRepo.h"
#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

namespace ArcMeta {

bool FavoritesRepo::add(const Favorite& fav) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO favorites (path, type, name, sort_order, added_at) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(fav.path));
    q.addBindValue(QString::fromStdWString(fav.type));
    q.addBindValue(QString::fromStdWString(fav.name));
    q.addBindValue(fav.sortOrder);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    bool ok = q.exec();
    if (ok) Database::instance().markDirty();
    return ok;
}

bool FavoritesRepo::remove(const std::wstring& path) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("DELETE FROM favorites WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    bool ok = q.exec();
    if (ok) Database::instance().markDirty();
    return ok;
}

std::vector<Favorite> FavoritesRepo::getAll() {
    std::vector<Favorite> results;
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT path, type, name, sort_order FROM favorites ORDER BY sort_order ASC", db);
    while (q.next()) {
        Favorite fav;
        fav.path = q.value(0).toString().toStdWString();
        fav.type = q.value(1).toString().toStdWString();
        fav.name = q.value(2).toString().toStdWString();
        fav.sortOrder = q.value(3).toInt();
        results.push_back(fav);
    }
    return results;
}

} // namespace ArcMeta
