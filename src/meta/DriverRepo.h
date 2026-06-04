#ifndef ARCMETA_DRIVER_REPO_H
#define ARCMETA_DRIVER_REPO_H

#include <string>
#include <vector>
#include <QStringList>
#include "MetadataDefs.h"

namespace ArcMeta {

struct DriverEntry {
    std::wstring volumePath; // e.g., "C:\"
    int rating;
    std::wstring color;
    std::vector<std::wstring> tags;
    bool pinned;
    std::wstring note;
    std::wstring url;
    std::vector<PaletteEntry> palettes;

    DriverEntry() : rating(0), pinned(false) {}
};

class DriverRepo {
public:
    static std::vector<DriverEntry> loadAll();
    static bool saveAll(const std::vector<DriverEntry>& entries);
    static bool update(const DriverEntry& entry);
};

} // namespace ArcMeta

#endif // ARCMETA_DRIVER_REPO_H
