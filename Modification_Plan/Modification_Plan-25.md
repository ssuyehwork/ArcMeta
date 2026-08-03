# 修复拖拽或导入新资产时托管库计数始终显示为 0 的问题 —— Modification_Plan-25.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的托管库中，所有导入、搬运的受控资产都是以高内聚的 `.arc` 资产包（物理上为文件夹）形式进行存储、激活和管理。然而，在元数据层触发内存缓存与侧边栏计数增量同步时，系统仅依赖于单纯的 `!isFolder`（即非文件夹资产才计入“全部数据”、“未分类”、“未标签”）这一决策判定。这导致合法的 `.arc` 资产包由于在物理上是文件夹，在激活和销毁入口中均被过滤拦截，致使“全部数据”和“未分类”等计数始终为 `0`，无法在拖拽和导入动作完成后实时准确地递增刷新。

为了彻底解决这一问题，本方案直接从源头逻辑重构 `MetadataManager`，使 `.arc` 资产包被完美识别为“逻辑文件资产”从而正常触发计数更新，并在计数更新外侧加上库内作用域护盾（`isInsideManagedLibrary`），确保磁盘导航模式下浏览、双击任何库外物理文件时，其内存缓存激活绝对不会溢流和污染托管库的侧边栏计数，完美巩固物理双轨制纯净隔离。

## 2. 问题定位
经过严密审计，导致拖拽导入不刷新计数且计数始终为 0 的代码位于以下 5 处决策入口：

1. **`MetadataManager::ensureActivated` (资产激活计数自增)**：
   - **定位**：`src/meta/MetadataManager.cpp`。写入 `m_cache[nPath] = rm` 后，直接执行 `if (!rm.isFolder)`，使得物理上为 `.arc` 文件夹容器的受控项目无法更新 `s_totalCount`、`s_untaggedCount` 及 `s_uncategorizedCount`。
2. **`MetadataManager::removeMetadataBatchSync` (批量删除计数自减)**：
   - **定位**：`src/meta/MetadataManager.cpp`。在批量移出及清除元数据缓存时，通过 `if (!it->second.isFolder)` 自减原子计数，从而错过了 `.arc` 文件夹资产包的递减。
3. **`MetadataManager::removeMetadataSingle` (单项删除计数自减)**：
   - **定位**：`src/meta/MetadataManager.cpp`。单项物理移出时使用 `if (!it->second.isFolder)` 对原子计数执行了过滤，漏掉了 `.arc` 资产包。
4. **`MetadataManager::updateTags` (打标签更新计数联动)**：
   - **定位**：`src/meta/MetadataManager.cpp`。在打标更新未标签计数时，使用了 `if (!isFolder)`。
5. **`MetadataManager::moveToTrash` 与 `MetadataManager::updateRating` (回收站移入移出计数联动)**：
   - **定位**：`src/meta/MetadataManager.cpp`。执行移入移出回收站逻辑（即 `isTrash` 状态更新）时，限制在了 `if (!isFolder)`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 导入新项目时计数始终显示为 0 (对应用户原话：“项目已经被创建到托管库了，但计数为何总是0？”) | 4.1、4.2 节重构 `ensureActivated`、`removeMetadataBatchSync`、`removeMetadataSingle`，使 `.arc` 受控资产包能够正常增减原子计数 | ✅ 一致 |
| 2    | 双轨制物理隔离 (对应用户原话及我的理解) | 4.1-4.5 节所有涉及原子计数更新的操作外，均用 `isInsideManagedLibrary(nPath)` 进行安全包裹保护 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/meta/MetadataManager.cpp` 顶部引入“逻辑资产”判定函数
在头文件包含后，引入静态函数 `isLogicalAssetPath`：

```
<<<<<<< SEARCH
#include "../ui/MediaColorExtractor.h"
=======
#include "../ui/MediaColorExtractor.h"

static bool isLogicalAssetPath(bool isFolder, const std::wstring& path) {
    if (!isFolder) return true;
    return QString::fromStdWString(path).endsWith(L".arc", Qt::CaseInsensitive);
}
>>>>>>> REPLACE
```

### 4.2 重构 `MetadataManager::ensureActivated` 的激活计数更新逻辑
将对 `!rm.isFolder` 的过滤升级为 `isInsideManagedLibrary` 护盾及 `isLogicalAssetPath` 判定：

```
<<<<<<< SEARCH
        m_cache[nPath] = rm;
        if (!rm.isFolder) {
            CategoryRepo::s_totalCount.fetch_add(1);
            if (rm.tags.isEmpty()) {
                CategoryRepo::s_untaggedCount.fetch_add(1);
            } else {
                std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
                for (const auto& t : rm.tags) {
                    if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                        CategoryRepo::s_globalTagsSet.insert(t);
                        CategoryRepo::s_tagsCount.fetch_add(1);
                    }
                }
            }
            if (CategoryRepo::getItemCategoryIds(rm.folderId, nPath).empty()) {
                CategoryRepo::s_uncategorizedCount.fetch_add(1);
            }
        }
        if (!rm.folderId.empty()) {
=======
        m_cache[nPath] = rm;
        if (isInsideManagedLibrary(nPath)) {
            if (isLogicalAssetPath(rm.isFolder, nPath)) {
                CategoryRepo::s_totalCount.fetch_add(1);
                if (rm.tags.isEmpty()) {
                    CategoryRepo::s_untaggedCount.fetch_add(1);
                } else {
                    std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
                    for (const auto& t : rm.tags) {
                        if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                            CategoryRepo::s_globalTagsSet.insert(t);
                            CategoryRepo::s_tagsCount.fetch_add(1);
                        }
                    }
                }
                if (CategoryRepo::getItemCategoryIds(rm.folderId, nPath).empty()) {
                    CategoryRepo::s_uncategorizedCount.fetch_add(1);
                }
            }
        }
        if (!rm.folderId.empty()) {
>>>>>>> REPLACE
```

### 4.3 重构 `MetadataManager::removeMetadataBatchSync` 的清除计数逻辑
修改对计数器递减和 `totalDelta` 计算的时序包裹：

```
<<<<<<< SEARCH
                if (!it->second.isFolder) {
                    if (it->second.isTrash) {
                        CategoryRepo::s_trashCount.fetch_sub(1);
                    } else {
                        CategoryRepo::s_totalCount.fetch_sub(1);
                        if (it->second.tags.isEmpty()) {
                            CategoryRepo::s_untaggedCount.fetch_sub(1);
                        }
                        if (CategoryRepo::getItemCategoryIds(it->second.folderId, curPath).empty()) {
                            CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                        }
                    }
                }

                if (!it->second.isFolder && !it->second.isTrash) {
                    totalDelta--;
                }
=======
                if (isInsideManagedLibrary(curPath)) {
                    if (isLogicalAssetPath(it->second.isFolder, curPath)) {
                        if (it->second.isTrash) {
                            CategoryRepo::s_trashCount.fetch_sub(1);
                        } else {
                            CategoryRepo::s_totalCount.fetch_sub(1);
                            if (it->second.tags.isEmpty()) {
                                CategoryRepo::s_untaggedCount.fetch_sub(1);
                            }
                            if (CategoryRepo::getItemCategoryIds(it->second.folderId, curPath).empty()) {
                                CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                            }
                        }
                    }
                }

                if (isInsideManagedLibrary(curPath)) {
                    if (isLogicalAssetPath(it->second.isFolder, curPath) && !it->second.isTrash) {
                        totalDelta--;
                    }
                }
>>>>>>> REPLACE
```

### 4.4 重构 `MetadataManager::removeMetadataSingle` 的清除计数逻辑
保持与批量相同的处理精度：

```
<<<<<<< SEARCH
                if (!it->second.isFolder) {
                    if (it->second.isTrash) {
                        CategoryRepo::s_trashCount.fetch_sub(1);
                    } else {
                        CategoryRepo::s_totalCount.fetch_sub(1);
                        if (it->second.tags.isEmpty()) {
                            CategoryRepo::s_untaggedCount.fetch_sub(1);
                        }
                        if (CategoryRepo::getItemCategoryIds(it->second.folderId, p).empty()) {
                            CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                        }
                    }
                }

                if (!it->second.isFolder && !it->second.isTrash) {
                    totalDelta--;
                }
=======
                if (isInsideManagedLibrary(p)) {
                    if (isLogicalAssetPath(it->second.isFolder, p)) {
                        if (it->second.isTrash) {
                            CategoryRepo::s_trashCount.fetch_sub(1);
                        } else {
                            CategoryRepo::s_totalCount.fetch_sub(1);
                            if (it->second.tags.isEmpty()) {
                                CategoryRepo::s_untaggedCount.fetch_sub(1);
                            }
                            if (CategoryRepo::getItemCategoryIds(it->second.folderId, p).empty()) {
                                CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                            }
                        }
                    }
                }

                if (isInsideManagedLibrary(p)) {
                    if (isLogicalAssetPath(it->second.isFolder, p) && !it->second.isTrash) {
                        totalDelta--;
                    }
                }
>>>>>>> REPLACE
```

### 4.5 重构 `MetadataManager::updateTags` 及移入回收站的计数联动逻辑
修改标签和垃圾箱计数触发入口：

```
<<<<<<< SEARCH
        if (!isFolder) {
            if (oldEmpty && !newEmpty) {
                CategoryRepo::s_untaggedCount.fetch_sub(1);
            } else if (!oldEmpty && newEmpty) {
                CategoryRepo::s_untaggedCount.fetch_add(1);
            }

            // Update global tags and tagsCount
            std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
            for (const auto& t : tags) {
                if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                    CategoryRepo::s_globalTagsSet.insert(t);
                    CategoryRepo::s_tagsCount.fetch_add(1);
                }
            }
        }
=======
        if (isInsideManagedLibrary(nPath)) {
            if (isLogicalAssetPath(isFolder, nPath)) {
                if (oldEmpty && !newEmpty) {
                    CategoryRepo::s_untaggedCount.fetch_sub(1);
                } else if (!oldEmpty && newEmpty) {
                    CategoryRepo::s_untaggedCount.fetch_add(1);
                }

                // Update global tags and tagsCount
                std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
                for (const auto& t : tags) {
                    if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                        CategoryRepo::s_globalTagsSet.insert(t);
                        CategoryRepo::s_tagsCount.fetch_add(1);
                    }
                }
            }
        }
>>>>>>> REPLACE
```

针对 `moveToTrash` (回收站移入移出核心)：

```
<<<<<<< SEARCH
        if (!isFolder) {
            if (isTrash) {
                CategoryRepo::s_totalCount.fetch_sub(1);
                CategoryRepo::s_trashCount.fetch_add(1);
                if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_sub(1);
                if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                    CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                }
            } else {
                CategoryRepo::s_totalCount.fetch_add(1);
                CategoryRepo::s_trashCount.fetch_sub(1);
                if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_add(1);
                if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                    CategoryRepo::s_uncategorizedCount.fetch_add(1);
                }
            }
        }
=======
        if (isInsideManagedLibrary(nPath)) {
            if (isLogicalAssetPath(isFolder, nPath)) {
                if (isTrash) {
                    CategoryRepo::s_totalCount.fetch_sub(1);
                    CategoryRepo::s_trashCount.fetch_add(1);
                    if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_sub(1);
                    if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                        CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                    }
                } else {
                    CategoryRepo::s_totalCount.fetch_add(1);
                    CategoryRepo::s_trashCount.fetch_sub(1);
                    if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_add(1);
                    if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                        CategoryRepo::s_uncategorizedCount.fetch_add(1);
                    }
                }
            }
        }
>>>>>>> REPLACE
```

针对 `updateRating` 中的移入回收站备份分流：

```
<<<<<<< SEARCH
    if (changed && !isFolder) {
        if (isTrash) {
            CategoryRepo::s_totalCount.fetch_sub(1);
            CategoryRepo::s_trashCount.fetch_add(1);
            if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_sub(1);
            if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                CategoryRepo::s_uncategorizedCount.fetch_sub(1);
            }
        } else {
            CategoryRepo::s_totalCount.fetch_add(1);
            CategoryRepo::s_trashCount.fetch_sub(1);
            if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_add(1);
            if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                CategoryRepo::s_uncategorizedCount.fetch_add(1);
            }
        }
    }
=======
    if (changed && isInsideManagedLibrary(nPath)) {
        if (isLogicalAssetPath(isFolder, nPath)) {
            if (isTrash) {
                CategoryRepo::s_totalCount.fetch_sub(1);
                CategoryRepo::s_trashCount.fetch_add(1);
                if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_sub(1);
                if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                    CategoryRepo::s_uncategorizedCount.fetch_sub(1);
                }
            } else {
                CategoryRepo::s_totalCount.fetch_add(1);
                CategoryRepo::s_trashCount.fetch_sub(1);
                if (oldEmpty) CategoryRepo::s_untaggedCount.fetch_add(1);
                if (CategoryRepo::getItemCategoryIds(fid, nPath).empty()) {
                    CategoryRepo::s_uncategorizedCount.fetch_add(1);
                }
            }
        }
    }
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` (添加 isLogicalAssetPath 判定函数并包裹 5 处计数更新)

**明确禁止越界修改的范围：**
- [ ] `src/meta/CategoryRepo.cpp` 的分类数据库操作 —— 不修改。
- [ ] 磁盘模式（DiskNav）及外部磁盘 JSON 写入逻辑 —— 不修改。

## 6. 实现准则与预警【核心】
1. **防范空路径崩溃**：`QString::fromStdWString(path)` 在极端场景下可处理任意空字符或异常反斜杠。
2. **零编译代价**：该改动完全在 `MetadataManager.cpp` 的私有作用域（静态辅助函数和内部方法）中完成，不需要增改任何公共头文件方法或属性，对其他物理模块没有任何耦合副作用，百分百保持极高内聚。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|--------------------------------------------|----------------|
| 双轨制路由分流 | 各自独立，逻辑分类写入 SQLite，磁盘导航 100% 独立，写入 `ArcMeta.cache` 离散缓存。 | ✅ 符合。添加了 `isInsideManagedLibrary` 阻断器，确保磁盘导航不污染 SQLite 内存计数。 |
| 统一数据来源判断复用 | 视图必须复用 `isMirrorSource()` 或数据源契约。 | ✅ 符合。 |

## 8. 待确认事项（可选）
- **无**。
