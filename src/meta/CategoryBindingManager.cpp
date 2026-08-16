#include "CategoryBindingManager.h"
#include "CategoryRepo.h"
#include "MetadataManager.h"

namespace ArcMeta {

CategoryBindingManager& CategoryBindingManager::instance() {
    static CategoryBindingManager inst;
    return inst;
}

bool CategoryBindingManager::bindAssetToCategory(const std::wstring& path, int categoryId) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::wstring nPath = MetadataManager::normalizePath(path);
    m_categoryToAssets[categoryId].insert(nPath);
    m_assetToCategories[nPath].insert(categoryId);
    std::string fid = MetadataManager::instance().getFolderIdSync(nPath);
    if (!fid.empty()) {
        return CategoryRepo::addItemToCategory(categoryId, fid, nPath);
    }
    return false;
}

bool CategoryBindingManager::unbindAssetFromCategory(const std::wstring& path, int categoryId) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::wstring nPath = MetadataManager::normalizePath(path);
    if (m_categoryToAssets.count(categoryId)) {
        m_categoryToAssets[categoryId].erase(nPath);
    }
    if (m_assetToCategories.count(nPath)) {
        m_assetToCategories[nPath].erase(categoryId);
    }
    std::string fid = MetadataManager::instance().getFolderIdSync(nPath);
    if (!fid.empty()) {
        return CategoryRepo::removeItemFromCategory(categoryId, fid);
    }
    return false;
}

std::vector<std::wstring> CategoryBindingManager::getAssetsInCategory(int categoryId) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<std::wstring> result;
    auto it = m_categoryToAssets.find(categoryId);
    if (it != m_categoryToAssets.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

} // namespace ArcMeta
