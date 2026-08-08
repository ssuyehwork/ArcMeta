# 治愈回收站恢复计数失真与 SCCH 大清洗 —— Modification_Plan-45.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目历史迭代中，侧边栏存在两处严重的硬伤：
1. **SCCH 逻辑残留**：在内存分类加载及计数对账中，依然残留着绕过数据库、使用物理路径前缀模糊匹配内存缓存的 SCCH 冗余分支。
2. **回收站恢复未分类计数偏离失真**：当用户从回收站中批量恢复资产时，系统会物理擦除回收站（`-8`）并将其显式归入未分类（`-2`）桶，但原子计数器 `s_uncategorizedCount` 和 `s_trashCount` 以及底层的持久化统计状态却完全遗漏了更新逻辑。
本方案将对上述两大 Bug 进行系统化的一网打尽，彻底净化侧边栏底层计数，并严格遵循 `Architecture and Planning.md` 第 1.2 节、2.1.1 节及 2.1.1.1 节。

## 2. 问题定位
1. **SCCH 加载逻辑残留**：`src/core/CategoryLoadService.cpp` 的 `loadCategoryItems` 存在对 `cat.parentId == 0` 的根分类前缀匹配内存扫描。
2. **SCCH 计数对账残留**：`src/meta/CategoryRepo.cpp` 的 `getCounts` 末尾存在对托管库根分类进行全小写前缀匹配的 `forEachCachedItem` 冗余计数。
3. **回收站恢复计数偏离失真**：`src/meta/CategoryRepo.cpp` 的 `restoreFromTrashBatch` 执行资产重定向时，没有同步执行计数器及底层 `system_stats` 数据库统计值的增量自愈。
4. **过时/不实注释**：清理提及 SCCH 分离模式的不实注释。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 彻底清洗 SCCH 逻辑（对应用户原话：“彻底排查全项目中残留的 SCCH 架构分支... 统一收拢到数据库(SQLite)单一实现”） | 物理删除 `CategoryLoadService.cpp` 与 `CategoryRepo.cpp` 中的冗余物理路径匹配分支并统一入口 | ✅ 一致 |
| 2    | 从回收站恢复资产计数失真自愈（对应用户原话：“restoreFromTrashBatch 这里明明也让一批资产变成了'未分类'状态... 却完全没有调用 s_uncategorizedCount.fetch_add()... 将这两样一起整理成修复任务书”） | 在 `restoreFromTrashBatch` 成功恢复资产后，批量自愈更新 `s_uncategorizedCount`、`s_trashCount` 及持久化统计数据库 | ✅ 一致 |
| 3    | 过时/不实注释清理（对应用户原话：“必须删除: 提及'SCCH 架构'...等描述性/已过时的架构说明注释”） | 清理 `CategoryRepo.h`、`ModelContract.h`、`BatchRenameEngine.cpp` 等中不实 SCCH 注释，更正为中性词 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 物理删除 `CategoryLoadService.cpp` 中的 SCCH 加载分支

```
<<<<<<< SEARCH
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
=======
    // 2. 统一使用数据库模式加载关联分类项
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
>>>>>>> REPLACE
```

### 4.2 物理删除 `CategoryRepo.cpp` 中的冗余对账计数分支，并更正 `.scch` 文件过滤注释

```
<<<<<<< SEARCH
    // 针对 parent_id = 0 的托管仓库根分类，全小写规范化对账核算资产总数 
    auto allCats = getAll(); 
    for (const auto& cat : allCats) { 
        if (cat.parentId == 0 && !cat.physicalPath.empty()) { 
            std::wstring normCatPath = MetadataManager::normalizePath(cat.physicalPath); 
            int count = 0; 
 
            if (!normCatPath.empty()) { 
                MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) { 
                    if (meta.isTrash || meta.isFolder) return; 
 
                    // 规范化全小写匹配，彻底消除 G:\ 与 g:\ 造成的归零 Bug 
                    if (path.rfind(normCatPath, 0) == 0) { 
                        count++; 
                    } 
                }); 
            } 
 
            bool updated = false; 
            for (auto& pair : res) { 
                if (pair.first == cat.id) { 
                    pair.second = std::max(pair.second, count); 
                    updated = true; 
                    break; 
                } 
            } 
            if (!updated) { 
                res.push_back({cat.id, count}); 
            } 
        } 
    } 
 
    cachedCounts = res; 
    s_countsDirty.store(false); 
    return res;  
} 
=======
    cachedCounts = res; 
    s_countsDirty.store(false); 
    return res;  
} 
>>>>>>> REPLACE
```

将文件展示时 `.scch` 物理过滤的注释改成中性措辞（不改动逻辑）：

```
<<<<<<< SEARCH
        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与 SCCH 元数据文件
        QString qPath = QString::fromStdWString(path);
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) || 
            qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
            return;
        }
=======
        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与辅助元数据文件
        QString qPath = QString::fromStdWString(path);
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) || 
            qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
            return;
        }
>>>>>>> REPLACE
```

### 4.3 物理自愈 `CategoryRepo.cpp` 中的回收站恢复批量计数自愈

```
<<<<<<< SEARCH
bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& folderIds) {
    return executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        // 1. Remove from trash bucket
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db,
            "DELETE FROM category_items WHERE category_id = ? AND folder_id = ?",
            -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(delStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Add to "未分类" bucket
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, UNCATEGORIZED_CAT_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // 3. Clear is_trash flag in metadata cache + persist
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, false);
        }
        
        // 2026-06-xx 物理对账：恢复后触发全量统计重建
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        return true;
    });
}
=======
bool CategoryRepo::restoreFromTrashBatch(const std::vector<std::string>& folderIds) {
    if (folderIds.empty()) return true;

    bool ok = executeFidBatch(folderIds, [](sqlite3* db, const std::string& fid) {
        // 1. Remove from trash bucket
        sqlite3_stmt* delStmt;
        if (sqlite3_prepare_v2(db,
            "DELETE FROM category_items WHERE category_id = ? AND folder_id = ?",
            -1, &delStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(delStmt, 1, TRASH_CATEGORY_ID);
            sqlite3_bind_text(delStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(delStmt);
            sqlite3_finalize(delStmt);
        }
        // 2. Add to "未分类" bucket
        std::wstring path = MetadataManager::instance().getPathByFolderId(fid);
        sqlite3_stmt* insStmt;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)",
            -1, &insStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(insStmt, 1, UNCATEGORIZED_CAT_ID);
            sqlite3_bind_text(insStmt, 2, fid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(insStmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(insStmt, 4, static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            sqlite3_step(insStmt);
            sqlite3_finalize(insStmt);
        }
        // 3. Clear is_trash flag in metadata cache + persist
        if (!path.empty()) {
            MetadataManager::instance().setTrash(path, false);
        }
        
        // 2026-06-xx 物理对账：恢复后触发全量统计重建
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        return true;
    });

    if (ok) {
        int delta = static_cast<int>(folderIds.size());
        s_uncategorizedCount.fetch_add(delta);
        s_trashCount.fetch_sub(delta);
        updatePersistentStat("sys_uncategorized_count", delta);
        updatePersistentStat("sys_trash_count", -delta);
    }
    return ok;
}
>>>>>>> REPLACE
```

### 4.4 清理不实/过时注释

#### 4.4.1 修改 `src/meta/CategoryRepo.h` 描述注释

```
<<<<<<< SEARCH
/**
 * @brief 分类持久层
 * 彻底废除数据库，全量转向 SCCH 架构
 */
class CategoryRepo {
=======
/**
 * @brief 分类持久层，基于中心化数据库实现
 */
class CategoryRepo {
>>>>>>> REPLACE
```

#### 4.4.2 修改 `src/core/ModelContract.h`

```
<<<<<<< SEARCH
    ManagedRole         = Qt::UserRole + 105, // 是否受控 (已在 SCCH 中登记)
=======
    ManagedRole         = Qt::UserRole + 105, // 是否受控 (已在索引中登记)
>>>>>>> REPLACE
```

#### 4.4.3 修改 `src/meta/BatchRenameEngine.cpp`

```
<<<<<<< SEARCH
            // 2026-05-24 按照用户要求：彻底移除 SCCH 逻辑，仅需更新数据库路径索引
=======
            // 2026-05-24：更新数据库路径索引
>>>>>>> REPLACE
```

#### 4.4.4 修改 `src/meta/MetadataManager.h`

```
<<<<<<< SEARCH
     * @brief 2026-06-xx 按照用户要求：在 SCCH 内存模式下执行多维搜索
=======
     * @brief 2026-06-xx：在内存模式下执行多维搜索
>>>>>>> REPLACE
```

#### 4.4.5 修改 `src/ui/MainWindow.cpp`

```
<<<<<<< SEARCH
                // 2026-05-24 按照用户要求：彻底移除 SCCH，改为中心化异步持久化
=======
                // 2026-05-24：改为中心化异步持久化
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/CategoryLoadService.cpp` (行号: 44-70, 移除 SCCH 冗余加载分支)
- [ ] `src/meta/CategoryRepo.cpp` (行号: 368-402, 1048-1079, 1371, 移除冗余根分类对账，自愈更新恢复资产的原子及物理计数)
- [ ] `src/meta/CategoryRepo.h` (更正头文件描述)
- [ ] `src/core/ModelContract.h` (更正注释)
- [ ] `src/meta/BatchRenameEngine.cpp` (更正注释)
- [ ] `src/meta/MetadataManager.h` (更正注释)
- [ ] `src/ui/MainWindow.cpp` (更正注释)

**明确禁止越界修改的范围：**
- [ ] `isAuxiliaryFile` 及其他处对 `.scch` / `metadata.scch` 文件本身的物理过滤逻辑 —— 100% 保持不动。

## 6. 实现准则与预警【核心】
1. **彻底物理隔离与计数对账**：批量恢复时，采用原子变量 `fetch_add`/`fetch_sub` 与底层的 `updatePersistentStat` 同步自愈，确保计数与底层数据 100% 一致。
2. **零编译报错**：修改中不引入任何不必要的外部头文件，保持代码纯净。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 内存模式与磁盘目录模式运行时各自独立，互不共享路径。 | ✅ 符合（将分类枚举、计数和从回收站恢复时的归纳状态完全统一到 SQLite 数据路径实现，完全符合内存模式的纯净规范） |

## 8. 待确认事项（可选）
*无*
