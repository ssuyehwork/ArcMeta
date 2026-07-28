# 高频关联多维查询缺失关键索引极速对账加固 —— Modification_Plan-119.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的软件运行中，对文件进行打标签、归类关联、移出分类、重命名同步、以及点击分类展开 BFS 过滤时，均会高频地访问底层 SQLite 数据库。
然而通过对全系统 SQL 的静态审查发现，多条涉及热点业务逻辑的过滤查询路径正面临无索引可用的困境。例如复合主键的最左列限制导致 `file_id` 单独过滤失效，以及级联重命名 `LIKE` 匹配、递归查找子树中对 `parent_id` 的频发检索均没有任何索引保护。随着磁盘中托管文件和分类树规模增加，必然退化为耗时的全表线性扫描。因此，必须补充相应的专属索引以解决此架构性能隐患。

## 2. 问题定位
1. **`category_items` 表只过滤 `file_id` 无索引**：
   复合主键 `(category_id, file_id)` 无法加速 `WHERE file_id = ?` 过滤。这导致 `removeAllCategoriesBatch`、`moveToTrashBatch`、`getItemCategoryIds` 等高频同步和删除操作对 `category_items` 全表扫描。
2. **`categories` 表过滤 `parent_id` 无索引**：
   `findCategoryId`、`getSubtreeIds` 及 `remove()` 级联删除会热点递归调用 `WHERE parent_id = ?`。缺少索引会导致侧边栏每次根据分类过滤搜索或物理建树时发生叠加扫描。
3. **`category_items` 表级联重命名 `path_hint` 前缀 LIKE 无索引**：
   `renamePhysicalCategoryPath` 中的前缀 LIKE 查询 `WHERE path_hint LIKE ?` 在没有 `path_hint` 索引的情况下被迫退化为线性全扫。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 缺失索引一（优先级最高）：`category_items(file_id)` (对应用户原话) | 补齐 `CREATE INDEX IF NOT EXISTS idx_category_items_file_id ON category_items(file_id);` | ✅ |
| 2    | 缺失索引二：`categories(parent_id)` (对应用户原话) | 补齐 `CREATE INDEX IF NOT EXISTS idx_categories_parent_id ON categories(parent_id);` | ✅ |
| 3    | 缺失索引三：`category_items(path_hint)` (对应用户原话) | 补齐 `CREATE INDEX IF NOT EXISTS idx_category_items_path_hint ON category_items(path_hint);` | ✅ |
| 4    | 补齐 `categories(physical_path)` 索引 (对应用户原话) | 补齐 `CREATE INDEX IF NOT EXISTS idx_categories_physical_path ON categories(physical_path);` | ✅ |

## 4. 详细解决方案

在 `DatabaseManager.cpp` 初始化连接并迁移各表 DDL 的末尾，一次性以幂等方式执行新增的四个非最左复合索引，完全阻断热点查询下的全表扫描：

```cpp
// 2026-08-xx 补齐高频查询缺失的关键索引，完全阻断全表扫描
sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_category_items_file_id ON category_items(file_id);", nullptr, nullptr, nullptr);
sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_category_items_path_hint ON category_items(path_hint);", nullptr, nullptr, nullptr);
sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_categories_parent_id ON categories(parent_id);", nullptr, nullptr, nullptr);
sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_categories_physical_path ON categories(physical_path);", nullptr, nullptr, nullptr);
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.cpp`
  - 修改 `DatabaseManager::getMemoryDb` 的初始化末尾。加入幂等的四个 `CREATE INDEX IF NOT EXISTS` 命令，不改变任何表结构与其他 SQL。

**明确禁止越界修改的范围：**
- [ ] `sqlite3_exec` 其他既有表定义、WAL 配置与备份线程 —— 不修改
- [ ] 各查询 API 的 C++ 端实现与 SQL 拼接 —— 不修改

## 6. 实现准则与预警【核心】

1. **加装位置的准确性**：
   确保这些索引是在 `getMemoryDb` 将当前表定义建立完、且 DDL 迁移完成后才执行建立，防止因提前执行而出现“找不到表”或“找不到列”的错误。
2. **零副作用**：
   SQLite 中的 `CREATE INDEX IF NOT EXISTS` 为标准幂等操作，无任何并发写入时序和多线程冲突问题。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 数据库加索引 | 补齐的高频过滤检索索引应保持幂等，且明确只为符合前缀前向通配的字段提供性能支撑。 | ✅ 是。补齐的四个索引均采用 `IF NOT EXISTS` 幂等写法，仅针对精确查询和前缀 LIKE （如重命名 path_hint）进行加速。 |

## 8. 待确认事项（可选）
（无）
