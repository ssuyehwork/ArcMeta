# Analysis & Modification Plan - 回收站项目“全部数据”隔离逻辑审计

## 1. 现状分析 (Current State)
经排查，系统在处理“回收站 (Trash)”项目时，UI 表现与底层逻辑存在严重冲突，导致回收站文件无法在“全部数据”视图中被有效隔离。

### 1.1 视图加载漏洞 (List Logic)
在 `CategoryRepo::getSystemCategoryPaths` 函数中，对不同系统分类的路径提取逻辑如下：
*   **缺陷代码段：**
    ```cpp
    if (type == "all") match = true; // 漏洞：未判断 meta.isTrash
    else {
        if (meta.isTrash) return; // 仅在此分支隔离
        // ...
    }
    ```
*   **后果：** 当用户点击侧边栏的“全部数据”时，返回的列表中包含已被移入回收站的项目，用户感知上“删除”失效。

### 1.2 计数逻辑冲突 (Count Logic)
*   **侧边栏徽标 (Badge)：** `getSystemCounts` 内部正确使用了 `if (meta.isTrash) return;` 隔离计数。
*   **持久化账本 (DB)：** `MetadataManager::markAsTrash` 显式声明 `移入回收站不应减少“全部数据”计数`，且 `fullRecount` 将回收站项目计入 `STAT_TOTAL_FILES`。
*   **后果：** 产生“幽灵计数”。例如：侧边栏显示“全部数据 10”，但点击进入后，由于列表没过滤，可能看到 15 个文件（5个在回收站）。

## 2. 架构设计修正建议 (Proposed Changes)

为了实现彻底隔离，必须确立以下**核心原则**：
> **“全部数据” = “系统内所有非回收站、非失效的活跃文件总和”。**

### 2.1 修正视图提取 (CategoryRepo.cpp)
在 `getSystemCategoryPaths` 中，将 `all` 类型与其它活跃视图的过滤逻辑对齐。
```cpp
// 修正方案
if (type == "all") {
    if (meta.isTrash || meta.isInvalid) return; // 强制隔离
    match = true;
}
```

### 2.2 同步持久化计数 (MetadataManager.cpp)
修改 `markAsTrash` 的语义。移入回收站应被视为从“活跃池”进入“隔离池”。
*   当 `isTrash` 为 `true` 时，调用 `CategoryRepo::incrementTotalFileCount(-1)`。
*   当执行“还原”或 `setTrash(false)` 时，调用 `CategoryRepo::incrementTotalFileCount(1)`。

### 2.3 强化桶位清理 (CategoryRepo.cpp)
在 `moveToTrashBatch` 中，目前代码已包含 `DELETE FROM category_items WHERE file_id = ?`，这确保了文件不再属于任何自定义分类。建议在此基础上增加对 `MetadataManager::notifyUI(RefreshLevel::FullRebuild)` 的触发，以确保侧边栏数字实时刷新。

## 3. 结论 (Conclusion)
目前的隔离失效是由于**视图过滤条件不严谨**以及**计数的定义域模糊**导致的。通过统一“全部数据”的定义域（排除 isTrash），可以彻底解决文件“删而不走”的视觉 Bug。
