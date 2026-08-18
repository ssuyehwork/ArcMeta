#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace QuarkMeta {

class CategoryBindingManager : public QObject {
    Q_OBJECT
public:
    static CategoryBindingManager& instance();

    // 绑定资产到分类
    bool bindAssetToCategory(const std::wstring& path, int categoryId);
    // 从分类解绑资产
    bool unbindAssetFromCategory(const std::wstring& path, int categoryId);
    // 获取分类下的所有资产路径
    std::vector<std::wstring> getAssetsInCategory(int categoryId) const;

private:
    explicit CategoryBindingManager(QObject* parent = nullptr);

    mutable std::shared_mutex m_mutex;
    std::unordered_map<int, std::unordered_set<std::wstring>> m_categoryToAssets;
    std::unordered_map<std::wstring, std::unordered_set<int>> m_assetToCategories;
};

} // namespace QuarkMeta
