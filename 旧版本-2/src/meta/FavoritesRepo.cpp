#include "FavoritesRepo.h"
#include <QDataStream>
#include <QFile>
#include <QDateTime>
#include <algorithm>

namespace ArcMeta {

namespace ScchFavoritesEngine {

struct FavoritesHeader {
    char magic[4] = {'F', 'A', 'V', 'S'};
    uint32_t version = 3;
    uint32_t count = 0;
};

static QDataStream& operator<<(QDataStream& ds, const Favorite& f) {
    ds << QString::fromStdWString(f.path) << QString::fromStdWString(f.type) << QString::fromStdWString(f.name) << f.sortOrder;
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, Favorite& f) {
    QString path, type, name;
    ds >> path >> type >> name >> f.sortOrder;
    f.path = path.toStdWString(); f.type = type.toStdWString(); f.name = name.toStdWString();
    return ds;
}

static bool loadAll(std::vector<Favorite>& favs) {
    QFile file("arcmeta_favorites.scch");
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    FavoritesHeader header;
    file.read((char*)&header, sizeof(header));
    if (memcmp(header.magic, "FAVS", 4) != 0) return false;
    favs.clear();
    for (uint32_t i = 0; i < header.count; ++i) { Favorite f; ds >> f; favs.push_back(f); }
    return true;
}

static bool saveAll(const std::vector<Favorite>& favs) {
    QFile file("arcmeta_favorites.scch");
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    FavoritesHeader header;
    header.count = (uint32_t)favs.size();
    file.write((char*)&header, sizeof(header));
    for (const auto& f : favs) ds << f;
    return true;
}

} // namespace ScchFavoritesEngine

bool FavoritesRepo::add(const Favorite& fav) {
    std::vector<Favorite> favs;
    ScchFavoritesEngine::loadAll(favs);
    bool found = false;
    for (auto& f : favs) {
        if (f.path == fav.path) { f = fav; found = true; break; }
    }
    if (!found) favs.push_back(fav);
    return ScchFavoritesEngine::saveAll(favs);
}

bool FavoritesRepo::remove(const std::wstring& path) {
    std::vector<Favorite> favs;
    ScchFavoritesEngine::loadAll(favs);
    favs.erase(std::remove_if(favs.begin(), favs.end(), [&](const Favorite& f) {
        return f.path == path;
    }), favs.end());
    return ScchFavoritesEngine::saveAll(favs);
}

std::vector<Favorite> FavoritesRepo::getAll() {
    std::vector<Favorite> favs;
    ScchFavoritesEngine::loadAll(favs);
    std::sort(favs.begin(), favs.end(), [](const Favorite& a, const Favorite& b) { return a.sortOrder < b.sortOrder; });
    return favs;
}

} // namespace ArcMeta
