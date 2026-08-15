# 回收站数据脱离与四类计数扣减修复方案 —— Trash.md

> **核心原则**：只要一个文件/资产被移入到回收站，它在 **“全部数据”、“未分类”、“托管库”以及“自定义分类”** 这 4 个地方的计数必须立刻扣减/彻底清零（脱离关联）。

---

## 一、 问题根源分析 (Root Cause Analysis)

在现有的 `StatisticsService::computeSnapshotFromDb()` 统计逻辑与 `ShellHelper::moveToTrash` 回收站处理流程中，存在以下导致回收站数据没有与 4 类计数彻底脱离的 Bug：

1. **内存快照状态未同步**：当文件被移入回收站时，磁盘物理文件被移动到了 `.arcmeta/trash` 目录下，但内存中 `MetadataManager` 缓存的 `RuntimeMeta` 对象的 `isTrash` 属性没有同步被更新为 `true`。
2. **托管库路径包含误判**：在统计托管库数量时，代码直接检查物理路径是否以托管库路径开头（`normAsset.rfind(normLib, 0) == 0`）。由于回收站路径 `.arcmeta/trash` 仍然位于盘符根目录下（如 `Z:/.arcmeta/trash`），导致回收站里的资产依然被错误地计入了托管库 `libraryCounts` 中。
3. **未分类与自定义分类未做回收站前置排他判定**：未分类 `uncategorized` 和自定义分类 `userCategoryCounts` 的统计缺乏全局一票否决的 `isTrash`/`.arcmeta/trash` 路径拦截。

---

## 二、 核心修复技术方案 (Technical Solution)

### 1. 拦截第一防线：`StatisticsService.cpp` 重算逻辑改进

在 `StatisticsService::computeSnapshotFromDb()` 遍历内存资产 `forEachCachedItem` 时，必须将**回收站一票否决拦截**提升为第一优先级，同时兼顾 `meta.isTrash` 标志与路径中是否包含 `/.arcmeta/trash` 字符：

```cpp
MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
    if (meta.isFolder) return;

    // 🛡️ 第一防线：强力回收站拦截 (兼顾标志位与物理路径特征)
    bool isInTrash = meta.isTrash || 
                     (path.find(L"/.arcmeta/trash") != std::wstring::npos) ||
                     (path.find(L"\\.arcmeta\\trash") != std::wstring::npos);

    if (isInTrash) {
        libraryTrashCount++;
        return; // 🚨 绝对提前退出！绝不参与 全部数据、未分类、托管库、自定义分类 的任何计数！
    }

    // --- 只有非回收站资产才允许继续向下统计 ---

    // 1. 全部数据 (allCount)
    allCount++;

    // 2. 未标签 (untaggedCount)
    if (meta.tags.isEmpty()) {
        untaggedCount++;
    }

    // 3. 自定义分类 (userCategoryCounts) 与 未分类 (uncategorizedCount) 判定
    bool hasUserCat = false;
    for (int cid : meta.categoryIds) {
        if (userCatIds.count(cid)) {
            snapshot.userCategoryCounts[cid]++;
            hasUserCat = true;
        }
    }

    if (!hasUserCat) {
        uncategorizedCount++;
    }

    // 4. 托管库分账统计 (libraryCounts)
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::SystemLibrary && !cat.physicalPath.empty()) {
            std::wstring normLib = MetadataManager::normalizePath(cat.physicalPath);
            std::wstring normAsset = MetadataManager::normalizePath(path);
            if (normAsset.rfind(normLib, 0) == 0) {
                snapshot.libraryCounts[cat.id]++;
            }
        }
    }
});
```

---

### 2. 状态同步第二防线：`ShellHelper::moveToTrash` 内存与关联关系同步

在资产移入回收站时（`ShellHelper::moveToTrash` 或 `MetadataManager::markAsTrash`），必须同步完成以下数据清理：

1. **内存缓存更新**：
   ```cpp
   // 更新内存缓存中的 isTrash 标识与新路径
   RuntimeMeta meta = MetadataManager::instance().getMetaSync(destPath);
   meta.isTrash = true;
   MetadataManager::instance().updateMetaCache(destPath, meta);
   ```

2. **分类关系解除与彻底擦除**：
   ```cpp
   // 移入回收站时，解除在 category_items 表中的所有旧关联
   std::string fid = MetadataManager::instance().getFolderIdSync(destPath.toStdWString());
   CategoryRepo::removeAllCategories(fid);
   ```

3. **实时触发计数重算**：
   ```cpp
   // 广播异步重算通知，确保 UI 侧刷新最新计数
   StatisticsService::instance().requestFullRecountAsync();
   ```

---

## 三、 修复预期效果验证 (Expected Results)

| 分类类型 | 移入回收站前 (示例 5 个文件) | 移入回收站后 (修复预期) | 表现解释 |
| :--- | :--- | :--- | :--- |
| **回收站** | `(0)` | **`(5)`** | 准确包含被移入回收站的 5 个文件 |
| **全部数据** | `(5)` | **`(0)`** | 扣减 5 个回收站文件 |
| **未分类** | `(5)` | **`(0)`** | 扣减 5 个回收站文件 |
| **托管库 (如 arcmeta.library_z)** | `(5)` | **`(0)`** | 扣减 5 个回收站文件 |
| **自定义分类** | `(5)` | **`(0)`** | 扣减 5 个回收站文件 |

---
*文档建立时间：2026-08-13*
