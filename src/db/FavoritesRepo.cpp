#include "FavoritesRepo.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDateTime>
#include <algorithm>

namespace ArcMeta {

namespace ScchFavoritesEngine {

static QJsonObject loadFavoritesScch() {
    QFile file("arcmeta_favorites.scch");
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) return doc.object();
    }
    QJsonObject root;
    root["favorites"] = QJsonArray();
    return root;
}

static bool saveFavoritesScch(const QJsonObject& root) {
    QFile file("arcmeta_favorites.scch");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

} // namespace ScchFavoritesEngine

bool FavoritesRepo::add(const Favorite& fav) {
    QJsonObject root = ScchFavoritesEngine::loadFavoritesScch();
    QJsonArray favs = root["favorites"].toArray();
    QJsonArray updatedFavs;
    bool found = false;
    QString targetPath = QString::fromStdWString(fav.path);
    for (const auto& val : favs) {
        QJsonObject obj = val.toObject();
        if (obj["path"].toString() == targetPath) {
            QJsonObject newObj;
            newObj["path"] = targetPath;
            newObj["type"] = QString::fromStdWString(fav.type);
            newObj["name"] = QString::fromStdWString(fav.name);
            newObj["sort_order"] = fav.sortOrder;
            newObj["added_at"] = (double)QDateTime::currentMSecsSinceEpoch();
            updatedFavs.append(newObj);
            found = true;
        } else updatedFavs.append(obj);
    }
    if (!found) {
        QJsonObject newObj;
        newObj["path"] = targetPath;
        newObj["type"] = QString::fromStdWString(fav.type);
        newObj["name"] = QString::fromStdWString(fav.name);
        newObj["sort_order"] = fav.sortOrder;
        newObj["added_at"] = (double)QDateTime::currentMSecsSinceEpoch();
        updatedFavs.append(newObj);
    }
    root["favorites"] = updatedFavs;
    return ScchFavoritesEngine::saveFavoritesScch(root);
}

bool FavoritesRepo::remove(const std::wstring& path) {
    QJsonObject root = ScchFavoritesEngine::loadFavoritesScch();
    QJsonArray favs = root["favorites"].toArray();
    QJsonArray remainingFavs;
    QString targetPath = QString::fromStdWString(path);
    for (const auto& val : favs) if (val.toObject()["path"].toString() != targetPath) remainingFavs.append(val);
    root["favorites"] = remainingFavs;
    return ScchFavoritesEngine::saveFavoritesScch(root);
}

std::vector<Favorite> FavoritesRepo::getAll() {
    std::vector<Favorite> results;
    QJsonObject root = ScchFavoritesEngine::loadFavoritesScch();
    QJsonArray favs = root["favorites"].toArray();
    for (const auto& val : favs) {
        QJsonObject obj = val.toObject();
        Favorite fav;
        fav.path = obj["path"].toString().toStdWString();
        fav.type = obj["type"].toString().toStdWString();
        fav.name = obj["name"].toString().toStdWString();
        fav.sortOrder = obj["sort_order"].toInt();
        results.push_back(fav);
    }
    std::sort(results.begin(), results.end(), [](const Favorite& a, const Favorite& b) { return a.sortOrder < b.sortOrder; });
    return results;
}

} // namespace ArcMeta
