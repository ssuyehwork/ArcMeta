# 架构分析与修改方案 - 侧边栏分类删除与回收站逻辑重构 (V2)

## 1. 现状分析
之前的实现虽然将文件标记为 `isTrash` 并解除了关联，但没有遵循用户要求的“回收站桶位”逻辑。用户希望回收站不仅是一个状态位，还是一个 ID 为 `-2` 的系统分类，所有被删除的分类项应汇聚于此。

## 2. 目标需求
- **系统分类预置**：数据库初始化时自动插入 ID 为 `-1` (未分类) 和 `-2` (回收站) 的分类记录。
- **删除分类入桶**：删除分类时，将该分类下所有文件的 `category_id` 更新为 `-2`，同时设置 `isTrash = 1`。
- **物理隔离加固**：计数逻辑（`getCounts`、`getSystemCounts` 等）必须通过 `category_id > 0` 显式排除系统分类。
- **还原与永久删除**：实现从回收站还原至“未分类”（ID `-1`）及基于 FID 的彻底删除逻辑。

## 3. 修改方案

### 3.1 数据库初始化 (src/meta/DatabaseManager.cpp)
在 `categories` 表创建后，执行 `INSERT OR IGNORE` 预置系统分类。

### 3.2 接口声明 (src/meta/CategoryRepo.h)
定义 `TRASH_CATEGORY_ID = -2` 和 `UNCATEGORIZED_CAT_ID = -1` 常量。
声明 `restoreFromTrashBatch` 和 `permanentlyDeleteBatch`。

### 3.3 逻辑重构 (src/meta/CategoryRepo.cpp)
- **`remove(int id)`**：
    1. 递归收集子分类。
    2. 提取所有唯一 FID。
    3. 事务处理：删除旧关联 -> 插入新关联（`category_id = -2`） -> 更新内存及磁盘的 `isTrash` 状态。
- **`restoreFromTrashBatch`**：
    1. 从回收站（-2）删除。
    2. 加入未分类（-1）。
    3. 清除 `isTrash` 标志。
- **计数过滤**：
    在所有统计 SQL 中增加 `WHERE category_id > 0`。

### 3.4 内存同步 (src/meta/MetadataManager.cpp)
实现 `setTrash` 接口，仅修改 `isTrash` 状态位及清除 `originalPath`（还原时），并触发防抖持久化。

## 4. 预期效果
1. 删除分类后，文件在“回收站”系统视图中可见。
2. 自定义分类的 Badge 计数不会包含回收站文件。
3. 从回收站还原的文件会正确出现在“未分类”中。
4. 永久删除将彻底移除所有数据库痕迹及分类关联。
