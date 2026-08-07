#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"
#include "../meta/DatabaseManager.h"
#include <QFileInfo>

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    // 🚨 修正：移除对 .arc 的过滤！.arc 是托管资源库真实的资产胶囊，绝非无用辅助文件
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤真正的辅助配置文件与缩略图
    }

    return false;
}
}

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
                if (isAuxiliaryFile(qPath)) {
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
                if (isAuxiliaryFile(qPath)) {
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
            if (isAuxiliaryFile(p)) {
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
        if (isAuxiliaryFile(qPath)) {
            return;
        }

        ItemRecord r = ItemRecord::create(qPath, &meta, true);
        r.groupName = "Library";
        libraryTrash.push_back(r);
    });

    // 2. 数据集 B：目录导航物理回收项
    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, trash_path, original_path, drive_letter, file_name, is_folder, file_size, deleted_at FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                const wchar_t* wTrashPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
                const wchar_t* wDriveLetter = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 3));
                const wchar_t* wFileName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
                int isFolder = sqlite3_column_int(stmt, 5);
                long long fileSize = sqlite3_column_int64(stmt, 6);
                long long deletedAt = sqlite3_column_int64(stmt, 7);

                if (wTrashPath && wOrigPath) {
                    ItemRecord r;
                    r.path = QString::fromWCharArray(wTrashPath);
                    r.originalPath = QString::fromWCharArray(wOrigPath);
                    r.filename = wFileName ? QString::fromWCharArray(wFileName) : QFileInfo(r.path).fileName();
                    r.isDir = (isFolder != 0);
                    r.size = fileSize;
                    r.mtime = deletedAt;
                    r.ctime = deletedAt;
                    r.atime = deletedAt;
                    r.isDiskTrash = true;
                    r.diskTrashId = id;
                    r.groupName = "DiskNav";

                    if (r.isDir) {
                        r.suffix = "";
                    } else {
                        int lastDot = r.filename.lastIndexOf('.');
                        r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
                    }

                    diskTrash.push_back(r);
                }
            }
            sqlite3_finalize(stmt);
        }
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
