#include "CategoryBindingManager.h"

namespace QuarkMeta {

CategoryBindingManager& CategoryBindingManager::instance() {
    static CategoryBindingManager inst;
    return inst;
}

CategoryBindingManager::CategoryBindingManager(QObject* parent)
    : QObject(parent) {
}

bool CategoryBindingManager::bindAssetToCategory(const std::wstring& path, int categoryId) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_categoryToAssets[categoryId].insert(path);
    m_assetToCategories[path].insert(categoryId);
    return true;
}

bool CategoryBindingManager::unbindAssetFromCategory(const std::wstring& path, int categoryId) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto catIt = m_categoryToAssets.find(categoryId);
    if (catIt != m_categoryToAssets.end()) {
        catIt->second.erase(path);
    }
    auto assetIt = m_assetToCategories.find(path);
    if (assetIt != m_assetToCategories.end()) {
        assetIt->second.erase(categoryId);
    }
    return true;
}

std::vector<std::wstring> CategoryBindingManager::getAssetsInCategory(int categoryId) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_categoryToAssets.find(categoryId);
    if (it == m_categoryToAssets.end()) {
        return {};
    }
    return std::vector<std::wstring>(it->second.begin(), it->second.end());
}

} // namespace QuarkMeta
