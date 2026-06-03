# ArcMeta 架构分析与演化方案

---

## 旧版本-1 多对多关联机制分析

### 旧版本数据结构

旧版本（`旧版本-1`）采用 **DB（SQLite）+ JSON 双轨制**，实现了完整的文件与分类之间的多对多关联。

#### 核心表结构（SQLite DB 模式）

```
categories        （分类表）
  id            INT  PRIMARY KEY
  parent_id     INT              ← 支持树形层级
  name          TEXT
  color         TEXT
  preset_tags   TEXT (JSON数组)
  sort_order    INT
  pinned        INT
  encrypted     INT
  encrypt_hint  TEXT

category_items   （分类-文件关联中间表）← 多对多关键
  category_id   INT   FK → categories.id
  file_id_128   TEXT  FK → items.file_id_128
  added_at      REAL  (时间戳)
```

#### JSON 模式（arcmeta_categories.json）

JSON 文件中同样维护了两个并行结构：
```json
{
  "categories": [ ...分类对象数组... ],
  "category_items": [
    { "category_id": 1, "file_id_128": "abc...", "added_at": 1234567890 },
    { "category_id": 2, "file_id_128": "abc...", "added_at": 1234567890 }
  ]
}
```

**关键特征**：同一个 `file_id_128` 可以出现在多个 `category_id` 下，一个分类也可以包含多个文件，这就是标准的**多对多（M:N）关联**。

---

## 当前版本 SCCH 多对多能力评估

### SCCH 架构的双层含义

当前版本的「SCCH」实际上涵盖了两套独立的 `.scch` 文件：

| 文件名 | 用途 | 存储位置 |
|---|---|---|
| `metadata.scch` | 离散元数据（单文件夹级别的条目元数据，如 rating/color/tags/note 等） | 每个被监控的文件夹内 |
| `arcmeta_categories.scch` | 分类与文件关联关系 | 程序工作目录（全局） |

### 多对多关联：已具备，但有缺陷

分析 [`CategoryRepo.cpp`](src/db/CategoryRepo.cpp) 中的 `ScchCategoryEngine` 命名空间可以发现：

**✅ 已具备的能力**

`arcmeta_categories.scch` 内部结构与旧版本 JSON 文件完全对应：

```json
{
  "categories": [ ...分类对象数组... ],
  "category_items": [
    { "category_id": 1, "file_id_128": "xxx", "added_at": 1234567890 },
    { "category_id": 1, "file_id_128": "yyy", "added_at": 1234567890 },
    { "category_id": 2, "file_id_128": "xxx", "added_at": 1234567890 }
  ]
}
```

同一个 `file_id_128` 可关联到多个分类，同一个分类也可包含多个文件，多对多关联在数据结构层面**完整保留**。

---

## ⚠️ 当前 SCCH 多对多关联存在的四大缺陷

### 缺陷 1：系统计数功能已被废除（严重）

```cpp
// CategoryRepo.cpp 当前版本：
int CategoryRepo::getUniqueItemCount() {
    return 0; // 彻底废除数据库，不再进行此类统计
}

int CategoryRepo::getUncategorizedItemCount() {
    return 0; // 彻底废除数据库，不再进行此类统计
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    return QMap<QString, int>(); // 彻底废除数据库，侧边栏系统项计数暂设为 0
}
```

**影响**：侧边栏的「全部」「今日」「昨日」「未分类」「未标签」「回收站」等系统计数全部返回 0，UI 功能严重退化。

---

### 缺陷 2：`getCounts()` 统计逻辑不对齐（中等）

```cpp
// 当前版本仅统计 category_items 里的原始记录数，未排重
static std::vector<std::pair<int, int>> getCounts() {
    ...
    countMap[catId] = countMap.value(catId, 0) + 1; // ← 未调用 COUNT(DISTINCT)
    ...
}
```

旧版本通过 SQL `COUNT(DISTINCT i.file_id_128)` 并 `JOIN items WHERE deleted=0` 来确保：
- 同一文件不重复计数
- 已删除文件不参与统计

当前版本 SCCH 模式下，如果一个文件 ID 因为某种原因被重复写入 `category_items`，计数将偏高；也无法感知文件是否已被删除（没有 `items` 表）。

---

### 缺陷 3：`getSystemCounts()` 依赖数据库查询无法直接迁移（中等）

旧版本的 `getSystemCounts()` 使用了复杂的 SQL 时间范围查询（今日/昨日/最近24小时访问），这些查询依赖 `items` 表中的 `ctime/mtime/atime` 字段。

当前 SCCH 体系中，`MetadataManager` 的内存缓存 `m_cache`（`unordered_map<wstring, RuntimeMeta>`）存储了 `rating/color/tags/note` 等信息，**但不存储 `ctime/mtime/atime` 时间戳**，`RuntimeMeta` 结构也未定义这些字段。

---

### 缺陷 4：`arcmeta_categories.scch` 整文件重写性能风险（低）

每次 `addItemToCategory` / `removeItemFromCategory` 操作都需要：
1. 读取整个 `arcmeta_categories.scch`
2. 在内存中修改 JSON
3. 覆盖写回整个文件

当分类条目数量较大时，这是一个 O(N) 的全量读写操作，存在性能隐患。而旧版本 SQLite 是行级操作，代价极低。

---

## 解决方案

### 方案总体原则

> **保持 SCCH 架构不变，在纯文件模式下实现全部统计功能，不再依赖任何数据库。**

---

### 方案 A：恢复 getSystemCounts() —— 基于 MetadataManager 内存缓存扫描

**核心思路**：`MetadataManager` 的 `m_cache` 存有所有已扫描文件的路径，通过 WinAPI 实时读取文件系统时间戳，在内存中完成统计。

#### 步骤一：在 `RuntimeMeta` 中补充时间戳字段

在 [`MetadataDefs.h`](src/meta/MetadataDefs.h) 的 `RuntimeMeta` 结构中添加：

```cpp
struct RuntimeMeta {
    int rating = 0;
    std::wstring color;
    QStringList tags;
    std::wstring note;
    std::wstring url;
    bool pinned = false;
    bool encrypted = false;
    std::vector<PaletteEntry> palettes;

    // 新增：文件系统时间戳（毫秒，加载时由 WinAPI 填充）
    long long ctime = 0;
    long long mtime = 0;
    long long atime = 0;
    long long fileSize = 0;
    bool isDeleted = false; // 标记文件是否已从磁盘删除
    std::string fileId128;  // 与 ItemMeta 对齐，用于关联查询
    ...
};
```

#### 步骤二：在 `MetadataManager::initFromScchMode()` 加载时填充时间戳

在扫描所有 `metadata.scch` 并填充 `m_cache` 时，同时通过 `GetFileAttributesExW` 获取 `ctime/mtime/atime`，存入对应的 `RuntimeMeta`。

#### 步骤三：实现纯内存版 `getSystemCounts()`

```cpp
QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> counts;
    auto& mgr = MetadataManager::instance();

    double now = (double)QDateTime::currentMSecsSinceEpoch();
    double startOfToday = (double)QDateTime(QDate::currentDate(), QTime(0,0)).toMSecsSinceEpoch();
    double startOfYesterday = startOfToday - 86400000.0;

    int all = 0, today = 0, yesterday = 0, recentlyVisited = 0;
    int untagged = 0;

    // 通过 MetadataManager 暴露一个只读遍历接口，扫描 m_cache
    mgr.forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.isDeleted) return;
        all++;
        if (meta.ctime >= startOfToday || meta.mtime >= startOfToday) today++;
        if ((meta.ctime >= startOfYesterday || meta.mtime >= startOfYesterday) &&
            (meta.ctime < startOfToday && meta.mtime < startOfToday)) yesterday++;
        if (meta.atime >= now - 86400000.0) recentlyVisited++;
        if (meta.tags.isEmpty()) untagged++;
    });

    counts["all"] = all;
    counts["today"] = today;
    counts["yesterday"] = yesterday;
    counts["recently_visited"] = recentlyVisited;
    counts["untagged"] = untagged;
    counts["uncategorized"] = getUncategorizedItemCount();
    counts["trash"] = 0; // 纯文件模式下回收站由系统负责，暂留扩展点
    return counts;
}
```

---

### 方案 B：修复 getCounts() —— 基于 file_id_128 去重

在 `ScchCategoryEngine::getCounts()` 中改用 `std::unordered_set` 去重：

```cpp
static std::vector<std::pair<int, int>> getCounts() {
    std::vector<std::pair<int, int>> counts;
    QJsonObject root = loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();

    // 按分类 ID 分组，使用 set 去重
    QMap<int, std::unordered_set<std::string>> catFileSets;
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        int catId = obj["category_id"].toInt();
        std::string fid = obj["file_id_128"].toString().toStdString();
        if (!fid.empty()) {
            catFileSets[catId].insert(fid);
        }
    }

    for (auto it = catFileSets.begin(); it != catFileSets.end(); ++it) {
        counts.push_back({it.key(), (int)it.value().size()});
    }
    return counts;
}
```

---

### 方案 C：修复 getUncategorizedItemCount() —— 基于缓存差集

```cpp
int CategoryRepo::getUncategorizedItemCount() {
    // 1. 从 arcmeta_categories.scch 收集所有已分类的 file_id_128
    QJsonObject root = ScchCategoryEngine::loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();
    std::unordered_set<std::string> categorizedIds;
    for (const auto& val : items) {
        std::string fid = val.toObject()["file_id_128"].toString().toStdString();
        if (!fid.empty()) categorizedIds.insert(fid);
    }

    // 2. 从 MetadataManager 遍历所有缓存条目，统计不在已分类集合中的数量
    int count = 0;
    MetadataManager::instance().forEachCachedItem(
        [&](const std::wstring&, const RuntimeMeta& meta) {
            if (meta.isDeleted) return;
            if (categorizedIds.find(meta.fileId128) == categorizedIds.end()) {
                count++;
            }
        });
    return count;
}
```

---

### 方案 D：为 MetadataManager 新增 forEachCachedItem 只读遍历接口

在 [`MetadataManager.h`](src/meta/MetadataManager.h) 中新增：

```cpp
/**
 * @brief 只读遍历内存缓存，用于统计等场景（持有读锁）
 * @param fn 回调函数，参数为 (path, RuntimeMeta)
 */
template<typename Func>
void forEachCachedItem(Func&& fn) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& [path, meta] : m_cache) {
        fn(path, meta);
    }
}
```

---

## 执行优先级建议

| 优先级 | 任务 | 影响范围 | 工作量 |
|---|---|---|---|
| 🔴 P0 | 修复 `getCounts()` 去重逻辑（方案B） | 分类条目数量显示准确性 | 小 |
| 🟠 P1 | 为 `RuntimeMeta` 补充时间戳和 fileId128 字段 | 为所有统计功能打基础 | 中 |
| 🟠 P1 | 为 `MetadataManager` 新增 `forEachCachedItem` 接口（方案D） | 解耦统计逻辑 | 小 |
| 🟡 P2 | 恢复 `getSystemCounts()` 纯内存实现（方案A） | 侧边栏系统项计数 | 中 |
| 🟡 P2 | 恢复 `getUncategorizedItemCount()` 纯内存实现（方案C） | 未分类统计 | 小 |
| 🟢 P3 | 考虑 `arcmeta_categories.scch` 写入加速（增量写入或内存缓存） | 大数据量性能 | 大 |

---

## 结论

**SCCH 体系已经具备多对多关联的数据结构**（`arcmeta_categories.scch` 中的 `category_items` 数组），其逻辑与旧版本 JSON 模式完全对等，文件换了扩展名，内部 schema 保持一致。

**当前真正缺失的不是多对多关联本身，而是基于多对多关联之上的统计与查询能力**：由于旧版本的这些统计是通过 SQLite SQL 语句完成的，迁移到纯文件模式后这些统计函数被简单地返回了 `0`，导致侧边栏功能退化。

以上方案 A～D 提供了一套完整的、不依赖数据库的纯内存/文件统计方案，可以在保持 SCCH 架构的前提下完整恢复全部统计功能。
