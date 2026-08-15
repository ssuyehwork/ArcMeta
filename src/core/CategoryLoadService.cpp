#include "CategoryLoadService.h" 
#include "../meta/MetadataManager.h" 
#include "../meta/CategoryRepo.h" 
#include "CategoryLockManager.h" 
#include "FileFilterService.h" 
#include "../meta/DiskTrashRepo.h" 
#include <QFileInfo> 
 
namespace ArcMeta { 

std::vector<ItemRecord> CategoryLoadService::loadCategoryItems(int categoryId, bool recursive) {
    std::vector<ItemRecord> allRecords;

    // 1. 获取当前分类元数据
    Category currentCat = CategoryRepo::getCachedById(categoryId);
    if (currentCat.id == 0) {
        currentCat = CategoryRepo::getById(categoryId);
    }

    // =========================================================================
    // 分支 A：系统托管根分类（SystemLibrary）—— 100% 对齐 StatisticsService 口径
    // 直接提取归属于该库物理路径的所有有效素材，彻底消除 category_items 关联断链造成的空白！
    // =========================================================================
    if (currentCat.kind == CategoryKind::SystemLibrary && !currentCat.physicalPath.empty()) {
        std::wstring normLibPath = MetadataManager::normalizePath(currentCat.physicalPath);
        if (!normLibPath.empty() && normLibPath.back() != L'\\' && normLibPath.back() != L'/') {
            normLibPath += L'\\';
        }

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
            // 过滤非受控、回收站、以及容器目录本身
            if (meta.isTrash || meta.isFolder) return;

            // 物理路径前缀比对，锁定属于当前盘托管库的素材
            std::wstring normAssetPath = MetadataManager::normalizePath(path);
            if (normAssetPath.rfind(normLibPath, 0) == 0) {
                // 安全加锁过滤
                if (!meta.folderId.empty() && isAssetLocked(meta.folderId)) {
                    return;
                }

                QString qPath = QString::fromStdWString(normAssetPath);
                if (FileFilterService::isAuxiliaryFile(qPath, false)) {
                    return;
                }

                allRecords.push_back(ItemRecord::create(qPath, &meta, true));
            }
        });

        return allRecords;
    }

    // =========================================================================
    // 分支 B：用户自定义逻辑分类（User）—— 走子分类及 category_items 关联加载
    // =========================================================================

    // 1. 加载直属子分类
    auto allCategories = CategoryRepo::getAll();
    for (const auto& cat : allCategories) {
        if (cat.parentId == categoryId && cat.kind != CategoryKind::SystemLibrary) {
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

    // 2. 加载关联资产项
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
            if (FileFilterService::isAuxiliaryFile(qPath, false)) {
                continue;
            }
            allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
        }
    }

    return allRecords;
}

std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (FileFilterService::isAuxiliaryFile(p, false)) {
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

std::vector<ItemRecord> CategoryLoadService::loadTrashItems() {
    std::vector<ItemRecord> libraryTrash;
    std::vector<ItemRecord> diskTrash;

    // 1. 数据集 A：资源库托管回收项
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (!meta.isTrash) return;

        // 过滤辅助文件
        QString qPath = QString::fromStdWString(path);
        if (FileFilterService::isAuxiliaryFile(qPath, false)) {
            return;
        }

        ItemRecord r = ItemRecord::create(qPath, &meta, true);
        r.groupName = "Library";
        libraryTrash.push_back(r);
    });

    // 2. 数据集 B：目录导航物理回收项（通过 DiskTrashRepo 获取，杜绝数据库句柄跨线程崩溃）
    auto trashItems = DiskTrashRepo::getAllTrashItems();
    for (const auto& item : trashItems) {
        ItemRecord r;
        r.path = QString::fromStdWString(item.trashPath);
        r.originalPath = QString::fromStdWString(item.originalPath);
        r.filename = !item.fileName.empty() ? QString::fromStdWString(item.fileName) : QFileInfo(r.path).fileName();
        r.isDir = item.isFolder;
        r.size = item.fileSize;
        r.mtime = item.deletedAt;
        r.ctime = item.deletedAt;
        r.atime = item.deletedAt;
        r.isDiskTrash = true;
        r.diskTrashId = item.id;
        r.groupName = "DiskNav";

        if (r.isDir) {
            r.suffix = "";
        } else {
            int lastDot = r.filename.lastIndexOf('.');
            r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
        }

        diskTrash.push_back(r);
    }

    std::vector<ItemRecord> allRecords;
    // 如果 Dataset A 非空，则加入 Group A 标题
    if (!libraryTrash.empty()) {
        ItemRecord hdr;
        hdr.isGroupHeader = true;
        hdr.groupName = "Library";
        hdr.filename = "【 资源库 - 托管资产 】";
        allRecords.push_back(hdr);
        allRecords.insert(allRecords.end(), libraryTrash.begin(), libraryTrash.end());
    }

    // 如果 Dataset B 非空，则加入 Group B 标题
    if (!diskTrash.empty()) {
        ItemRecord hdr;
        hdr.isGroupHeader = true;
        hdr.groupName = "DiskNav";
        hdr.filename = "【 目录导航 - 物理文件 】";
        allRecords.push_back(hdr);
        allRecords.insert(allRecords.end(), diskTrash.begin(), diskTrash.end());
    }

    return allRecords;
}

} // namespace ArcMeta
