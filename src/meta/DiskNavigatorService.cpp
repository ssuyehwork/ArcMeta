#include "DiskNavigatorService.h"
#include <filesystem>

namespace QuarkMeta {

DiskNavigatorService& DiskNavigatorService::instance() {
    static DiskNavigatorService inst;
    return inst;
}

std::unordered_map<std::wstring, ItemMeta> DiskNavigatorService::loadDirectoryItems(const std::wstring& folderPath) {
    return QuarkMetaJson::readFolderMeta(folderPath);
}

bool DiskNavigatorService::getItemMeta(const std::wstring& filePath, ItemMeta& outMeta) {
    std::filesystem::path p(filePath);
    std::wstring parentDir = p.parent_path().wstring();
    std::wstring fileName = p.filename().wstring();

    if (parentDir.empty()) return false;

    auto items = QuarkMetaJson::readFolderMeta(parentDir);
    auto it = items.find(fileName);
    if (it != items.end()) {
        outMeta = it->second;
        return true;
    }
    return false;
}

void DiskNavigatorService::saveItemMeta(const std::wstring& filePath, std::function<void(ItemMeta&)> updater) {
    QuarkMetaJson::updateItemMeta(filePath, updater);
}

void DiskNavigatorService::handleDiskRename(const std::wstring& oldPath, const std::wstring& newPath, bool isDir) {
    Q_UNUSED(isDir);
    QuarkMetaJson::migrateItemMetadata(QString::fromStdWString(oldPath), QString::fromStdWString(newPath));
}

} // namespace QuarkMeta
