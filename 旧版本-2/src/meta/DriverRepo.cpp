#include "DriverRepo.h"
#include <QDataStream>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <windows.h>

namespace ArcMeta {

namespace {
    QDataStream& operator<<(QDataStream& ds, const std::wstring& ws) {
        ds << QString::fromStdWString(ws);
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, std::wstring& ws) {
        QString s; ds >> s; ws = s.toStdWString();
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const PaletteEntry& p) {
        ds << static_cast<uint8_t>(p.color.red()) << static_cast<uint8_t>(p.color.green()) << static_cast<uint8_t>(p.color.blue()) << p.ratio;
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, PaletteEntry& p) {
        uint8_t r = 0, g = 0, b = 0; float ratio = 0.0f;
        ds >> r >> g >> b >> ratio;
        p.color = QColor(r, g, b); p.ratio = ratio;
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const DriverEntry& e) {
        ds << e.volumePath << e.rating << e.color;
        ds << static_cast<int>(e.tags.size());
        for (const auto& t : e.tags) ds << t;
        ds << e.pinned << e.note << e.url;
        ds << static_cast<int>(e.palettes.size());
        for (const auto& p : e.palettes) ds << p;
        return ds;
    }

    QDataStream& operator>>(QDataStream& ds, DriverEntry& e) {
        ds >> e.volumePath >> e.rating >> e.color;
        int tagCount = 0; ds >> tagCount;
        e.tags.clear();
        for (int i = 0; i < tagCount; ++i) { std::wstring t; ds >> t; e.tags.push_back(t); }
        ds >> e.pinned >> e.note >> e.url;
        int palCount = 0; ds >> palCount;
        e.palettes.clear();
        for (int i = 0; i < palCount; ++i) { PaletteEntry p; ds >> p; e.palettes.push_back(p); }
        return ds;
    }

    QString getRepoPath() {
        return QCoreApplication::applicationDirPath() + "/arcmeta_drivers.scch";
    }

    struct DriverHeader {
        char magic[4];
        uint32_t version;
        uint32_t count;
        DriverHeader() : version(1), count(0) {
            magic[0] = 'D'; magic[1] = 'R'; magic[2] = 'V'; magic[3] = '\0';
        }
    };
}

std::vector<DriverEntry> DriverRepo::loadAll() {
    std::vector<DriverEntry> entries;
    QFile file(getRepoPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return entries;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    DriverHeader header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return entries;
    if (memcmp(header.magic, "DRV\0", 4) != 0) return entries;

    for (uint32_t i = 0; i < header.count; ++i) {
        DriverEntry e;
        ds >> e;
        entries.push_back(e);
    }
    return entries;
}

bool DriverRepo::saveAll(const std::vector<DriverEntry>& entries) {
    QString path = getRepoPath();
    QString tmpPath = path + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    DriverHeader header;
    header.count = static_cast<uint32_t>(entries.size());
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (const auto& e : entries) ds << e;
    file.close();

    if (!MoveFileExW(tmpPath.toStdWString().c_str(), path.toStdWString().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool DriverRepo::update(const DriverEntry& entry) {
    std::vector<DriverEntry> entries = loadAll();
    bool found = false;
    for (auto& e : entries) {
        if (QString::fromStdWString(e.volumePath).toUpper() == QString::fromStdWString(entry.volumePath).toUpper()) {
            e = entry;
            found = true;
            break;
        }
    }
    if (!found) entries.push_back(entry);
    return saveAll(entries);
}

} // namespace ArcMeta
