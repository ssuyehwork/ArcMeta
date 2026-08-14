# 移除残留 SCCH 架构分支并收拢至 DB 路径 —— Modification_Plan-43.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在项目历史迭代演进中，遗留了 SCCH 分离模式架构下的分类加载分支逻辑。系统目前已经有完整的、落盘在数据库表（`category_items`）中的根分类绑定关系，因此需要移除通过物理路径前缀模糊匹配内存扫描（`MetadataManager::forEachCachedItem`）的冗余加载和计数分支，统一收拢到中心化 SQLite 单一实现。本方案承接自用户下达的“排查+移除 SCCH 架构分支”任务书，并严格遵循 `Development_Plan.md` 第 2 节。

## 2. 问题定位
1. **分类文件枚举逻辑残留**：在 `src/core/CategoryLoadService.cpp` 的 `loadCategoryItems` 函数中，存在 `cat.parentId == 0 && !cat.physicalPath.empty()` 的 if 判定分支，使用内存前缀扫描来加载根分类的文件列表。
2. **分类计数对账逻辑残留**：在 `src/meta/CategoryRepo.cpp` 的 `getCounts` 函数中，存在对 `cat.parentId == 0` 的根分类通过物理路径前缀扫描统计总条数的冗余逻辑。
3. **残留不合时宜的注释**：包括 `CategoryRepo.h`、`ModelContract.h` 等在内的多处注释提及了已废弃的 SCCH 架构或不实的描述。
4. **.scch 文件物理过滤需予以保留**：在 `ItemRecord.cpp`、`CategoryLoadService.cpp` 等中涉及的 `metadata.scch` 后缀名资产过滤用于确保在列表展示时不泄露辅助缓存，必须予以物理保留，不能删除。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 彻底排查全项目中残留的 SCCH 架构分支(代码逻辑,而非文件类型过滤),标记后移除，统一收拢到数据库单一实现（对应用户原话：“彻底排查全项目中残留的 SCCH 架构分支... 统一收拢到数据库(SQLite)单一实现”） | 精准定位到 `CategoryLoadService.cpp` 与 `CategoryRepo.cpp` 的冗余物理路径匹配分支，并进行删除/替换 | ✅ 一致 |
| 2    | 任何以"根分类物理路径前缀匹配 + forEachCachedItem 内存扫描"方式枚举分类文件的代码分支必须删除（对应用户原话：“必须删除: 任何以'根分类物理路径前缀匹配 + forEachCachedItem 内存扫描'方式... 这一整个 if 分支”） | 删除 `CategoryLoadService::loadCategoryItems` 中的 `cat.parentId == 0` 的 `if` 分支 | ✅ 一致 |
| 3    | 替换规则：删除SCCH分支后,该功能必须完整改为调用现有的数据库路径实现（对应用户原话：“替换规则:删除SCCH分支后,该功能必须完整改为调用现有的数据库路径实现”） | 分类项加载完全使用数据库路径（即 `CategoryRepo::getItemsInCategory` 或 `CategoryRepo::getItemsRecursive`） | ✅ 一致 |
| 4    | 必须保留：对 .scch / metadata.scch 后缀名的文件过滤逻辑（对应用户原话：“必须保留: 对 .scch / metadata.scch 后缀名的文件过滤逻辑... 逻辑本身不动”） | 物理保留 `isAuxiliaryFile` 等位置对 `.scch`/`metadata.scch` 的过滤代码，仅将注释文字修正为中性，不改动功能 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/core/CategoryLoadService.cpp` (移除 SCCH 枚举加载分支并保留辅助文件过滤)

修改 `loadCategoryItems`，移除对 `cat.parentId == 0` 时采用 `forEachCachedItem` 进行物理匹配的冗余加载分支，直接走向统一的数据库关联表项查询分支。

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

### 4.2 修改 `src/meta/CategoryRepo.cpp` (移除 `getCounts` 中冗余根分类前缀匹配对账计数，更正 `.scch` 过滤注释)

移除 `getCounts` 末尾针对根分类使用 `forEachCachedItem` 进行路径前缀模糊扫描的冗余物理前缀匹配逻辑，完全由数据库级 `category_items` 数据承载计数。更正文件物理过滤部分的注释。

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

```
<<<<<<< SEARCH
        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与 SCCH 元数据文件
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
            qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
            return;
        }
=======
        // 2. 核心修正：彻底过滤掉容器内部的辅助缩略图与辅助元数据文件
        if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
            qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
            return;
        }
>>>>>>> REPLACE
```

### 4.3 清理不实/过时注释

#### 4.3.1 修改 `src/meta/CategoryRepo.h` 描述注释

```
<<<<<<< SEARCH
/**
 * @brief 分类持久层
 * 彻底废除数据库，全量转向 SCCH 架构
 */
=======
/**
 * @brief 分类持久层，基于中心化数据库实现
 */
>>>>>>> REPLACE
```

#### 4.3.2 修改 `src/core/ModelContract.h`

```
<<<<<<< SEARCH
    ManagedRole         = Qt::UserRole + 105, // 是否受控 (已在 SCCH 中登记)
=======
    ManagedRole         = Qt::UserRole + 105, // 是否受控 (已在索引中登记)
>>>>>>> REPLACE
```

#### 4.3.3 修改 `src/meta/BatchRenameEngine.cpp`

```
<<<<<<< SEARCH
            // 2026-05-24 按照用户要求：彻底移除 SCCH 逻辑，仅需更新数据库路径索引
=======
            // 2026-05-24：更新数据库路径索引
>>>>>>> REPLACE
```

#### 4.3.4 修改 `src/meta/MetadataManager.h`

```
<<<<<<< SEARCH
     * @brief 2026-06-xx 按照用户要求：在 SCCH 内存模式下执行多维搜索
=======
     * @brief 2026-06-xx：在内存模式下执行多维搜索
>>>>>>> REPLACE
```

#### 4.3.5 修改 `src/ui/MainWindow.cpp`

```
<<<<<<< SEARCH
                // 2026-05-24 按照用户要求：彻底移除 SCCH，改为中心化异步持久化
=======
                // 2026-05-24：改为中心化异步持久化
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/CategoryLoadService.cpp` (行号: 44-70, 移除 SCCH 枚举加载分支)
- [ ] `src/meta/CategoryRepo.cpp` (行号: 1048-1079, 1371, 移除冗余根分类前缀匹配对账计数，更新注释)
- [ ] `src/meta/CategoryRepo.h` (更正头文件注释)
- [ ] `src/core/ModelContract.h` (更正注释)
- [ ] `src/meta/BatchRenameEngine.cpp` (更正注释)
- [ ] `src/meta/MetadataManager.h` (更正注释)
- [ ] `src/ui/MainWindow.cpp` (更正注释)

**明确禁止越界修改的范围：**
- [ ] `isAuxiliaryFile` 及其他处对 `.scch` / `metadata.scch` 文件本身的物理过滤逻辑——不修改，仅能对注释进行微调。

## 6. 实现准则与预警【核心】
1. **防止编译问题**：在对 `CategoryLoadService.cpp` 修改时，不引入新头文件，原有的 `CategoryRepo::getItemsRecursive` 与 `CategoryRepo::getItemsInCategory` 已经是完备的无缝替代，可以直接替代。
2. **零脑补改动**：机械且绝对严格地按照物理替换块进行，绝不添加或变动业务本身。
3. **安全自检**：在完成物理代码修改后，必须重新搜索 `SCCH`/`scch`（排除必须保留的文件过滤逻辑），确认架构残留分支全部移除。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨路由物理隔离 | 托管库下进行逻辑处理（仅改写 SQLite 映射字段），磁盘模式下进行物理处理，两者彻底隔离。 | ✅ 符合（分类统计和加载现在完全由数据库表 `category_items` 主导，杜绝了靠模糊内存前缀路径匹配的跨域侵入） |
| 系统资产屏蔽保护 | 任何文件监控层及自动对账扫描生命周期，必须在监控信号源、对账及数据库对账的最前端进行强制拦截，防止把辅助资产导入侧边栏分类树。 | ✅ 符合（保留了对 `metadata.scch` 和 `_thumbnail.png` 辅助文件的物理屏蔽逻辑，只优化注释，坚守了资产屏蔽红线） |

## 8. 待确认事项（可选）
*无*
