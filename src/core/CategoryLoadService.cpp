#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"

namespace ArcMeta {

std::vector<ItemRecord> CategoryLoadService::loadCategoryItems(int categoryId, bool recursive) {
    std::vector<ItemRecord> allRecords;

    // 1. 加载子分类
    auto allCategories = CategoryRepo::getAll();
    for (const auto& cat : allCategories) {
        if (cat.parentId == categoryId) {
            ItemRecord r;
            r.isCategory = true;
            r.categoryId = cat.id;
            r.categoryName = QString::fromStdWString(cat.name);
            r.categoryColor = QString::fromStdWString(cat.color).isEmpty() ? "#aaaaaa" : QString::fromStdWString(cat.color);
            r.rating = 0;
            r.pinned = cat.pinned;
            r.path = QString::fromStdWString(cat.physicalPath);
            allRecords.push_back(r);
        }
    }

    // 2. 加载文件 (SCCH 分离模式)
    std::vector<CategoryItem> items;
    if (recursive) {
        items = CategoryRepo::getItemsRecursive(categoryId);
    } else {
        items = CategoryRepo::getItemsInCategory(categoryId);
    }

    allRecords.reserve(allRecords.size() + items.size());
    for (const auto& item : items) {
        std::wstring wPath = MetadataManager::instance().getPathByFolderId(item.folderId);
        if (wPath.empty() && !item.pathHint.empty()) {
            wPath = item.pathHint;
        }

        if (!wPath.empty()) {
            allRecords.push_back(ItemRecord::create(QString::fromStdWString(wPath), nullptr, true));
        }
    }

    return allRecords;
}

std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}

} // namespace ArcMeta
