#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"

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
    Category cat = CategoryRepo::getById(categoryId);
    if (cat.id > 0 && cat.parentId == 0 && !cat.physicalPath.empty()) {
        std::wstring normCatPath = MetadataManager::normalizePath(cat.physicalPath);
        if (!normCatPath.empty()) {
            MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
                if (meta.isTrash || meta.isFolder) return;

                QString qPath = QString::fromStdWString(path);
                if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
                    qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
                    return;
                }

                if (path.rfind(normCatPath, 0) == 0) {
                    if (isAssetLocked(meta.folderId)) {
                        return;
                    }
                    allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
                }
            });
        }
    } else {
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
                if (isAssetLocked(item.folderId)) {
                    continue;
                }
                QString qPath = QString::fromStdWString(wPath);
                if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
                    qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
                    continue;
                }
                allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
            }
        }
    }

    return allRecords;
}

std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (p.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            std::string assetId = MetadataManager::instance().getFolderIdSync(p.toStdWString());
            if (!assetId.empty() && isAssetLocked(assetId)) {
                continue;
            }
            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}

bool CategoryLoadService::isAssetLocked(const std::string& assetId) {
    if (assetId.empty()) return false;
    
    // 获取该资产绑定的所有自定义分类 ID
    std::vector<int> catIds = CategoryRepo::getItemCategoryIds(assetId);

    for (int cid : catIds) {
        Category cat = CategoryRepo::getById(cid);
        // 如果资产所属的任意分类处于加锁且未解锁状态，阻断展示
        if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(cid)) {
            return true; // 已被加锁隔离
        }
    }
    return false;
}

} // namespace ArcMeta
