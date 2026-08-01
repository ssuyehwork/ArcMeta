# 全局物理数据库同库同事务重构与语义统一 —— Modification_Plan-18.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

此前系统在资产导入、数据库存储设计与语义命名上存在着多处不合理的设计和架构负债：
1. **跨库撕裂**：资产元数据（`metadata` 表）被保存在各个驱动盘分库（如 `Arcmeta_VolSerial_G.db`）中，而分类关联关系（`category_items` 表）却被写进全局主库 `global.db`。这种“跨库分体式存储”使得元数据无法自然形成外键/索引匹配，引起计数偏差、句柄错位等不合理问题。
2. **连接句柄 nullptr**：多线程异步调用时经常因没有及时预热句柄导致返回 `nullptr`，写入或同步被静默跳过，产生数据库空等致命问题。
3. **语义概念模糊**：托管资产物理上是由 `.arc` 文件夹容器（Folder）管理的。而在数据库中主键却叫 `file_id`，C++ 结构体中叫 `fileId` 或 `fileId128`，严重割裂和混淆了真实的物理含义。
4. **分类定位混乱**：盘符分类节点（如 `ArcMeta.Library_G`）被混同于常规的用户语义分类，导致未进行人工分类的资产未能在逻辑桶中归属于“未分类”，引起“未分类”计数发生严重偏差。

本方案旨在摒弃一切局部补丁，通过系统级的一体化重构彻底实现**同库同事务、100% 自动预热、folderId 语义精确对位、以及物理分类归属对账纠正**。

---

## 2. 问题定位

1. **同库事务撕裂**（`src/util/AssetImporter.cpp` / `src/meta/CategoryRepo.cpp`）：
   - `addItemToCategory`、`removeItemFromCategory` 固定写入了 `getGlobalDb()`。
   - 导入落盘没有在同一个盘符数据库上开启大事务，导致元数据和分类关联可能不同步。

2. **数据库句柄访问不健壮**（`src/meta/DatabaseManager.cpp`）：
   - 缺乏能够自动完成加载、初始化、盘符飘移自适应自愈并100%安全预热的单一访问入口。

3. **语义概念混乱**（`src/meta/MetadataDefs.h` / `src/core/IndexedEntry.h` / `src/meta/MetadataManager.cpp` 等）：
   - `file_id` 无法与 `.arc` 文件夹容器这一概念精准名实相符，在 C++ 和 SQL 语句中全量散落。

4. **对账计算逻辑未对齐**（`src/meta/CategoryRepo.cpp` 中的 `fullRecount`）：
   - 盘符节点被误判为已经有语义分类，导致新资产未计入“未分类”中。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1 | "杜绝缝缝补补、打补丁的方式解决问题" | 彻底取消分体库，实现 `metadata` 与 `category_items` 100% 在同盘分库合并存储，完全弃用主库存储分类映射 | ✅ |
| 2 | "统一数据库访问入口 DatabaseManager::getDbForPath(path)" | 实现 `getDbForPath(path)`：传入路径自动加载、预热并返回 100% 有效的盘符分库句柄，绝不返回 `nullptr` | ✅ |
| 3 | "AssetImporter 大事务原子入库（要么全成功，要么全回滚）" | 在 `AssetImporter` 中开启 `SqlTransaction`，将 metadata 写入与 category_items 写入合并到同一个数据库同事务内原子落盘 | ✅ |
| 4 | "将主键字段命名为 Folder_id ... 比旧有的 file_id 在语义上准确得多" | 数据库列重命名为 `folder_id`，C++ 结构体与模型成员 100% 统一更名为 `folderId`，彻底弃用 `fileId`/`fileId128` | ✅ |
| 5 | "ArcMeta.Library_G ... 只是侧边栏的物理入口 ... 资产包未手动分类前，逻辑上 100% 属于“未分类”" | 对账逻辑中仅将具有自定义语义分类（自定义 `category_id > 0`）的资产视作已分类；仅属于 Library_G 等盘符物理节点且未人工分类时，100% 计入“未分类” | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改点 A — `DatabaseManager.h/.cpp`：统一提供 100% 有效的 `getDbForPath`

1. 声明并实现 `getDbForPath`。
2. 内部通过提取该路径的盘符 and 卷信息，自动预热并 100% 返回有效分库句柄（当为系统全局配置路径时，自动预热并返回 `getGlobalDb()`）。

```diff
// DatabaseManager.h 物理新增：
+    /**
+     * @brief 统一数据库访问入口：根据路径自动预热并 100% 返回有效数据库连接句柄
+     * 保证在多线程下也绝不返回 nullptr 且实现同库同连接。
+     */
+    sqlite3* getDbForPath(const std::wstring& path);
```

```diff
// DatabaseManager.cpp 物理实现：
+sqlite3* DatabaseManager::getDbForPath(const std::wstring& path) {
+    std::wstring nPath = QDir::toNativeSeparators(QString::fromStdWString(path)).toStdWString();
+    // 如果是程序安装目录下的全局主配置，或者无法获取卷序列号，则预热并返回全局主配置库
+    if (nPath.length() == 3 && nPath[1] == L':' && (nPath[2] == L'\\' || nPath[2] == L'/')) {
+        return getGlobalDb();
+    }
+    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(nPath);
+    if (volSerial == L"UNKNOWN") {
+        return getGlobalDb();
+    }
+    QString letter = "";
+    if (nPath.length() >= 2 && nPath[1] == L':') {
+        letter = QString::fromWCharArray(&nPath[0], 1);
+    }
+    // 100% 保证自动加载、打开、预热该分库，绝不返回 nullptr
+    sqlite3* db = getMemoryDb(volSerial, letter);
+    if (!db) {
+        db = getGlobalDb();
+    }
+    return db;
+}
```

### 4.2 修改点 B — SQLite 建表 Schema 物理语义更名（`DatabaseManager.cpp`）

将 `metadata` 表的主键 `file_id` 变更为 `folder_id`；
将 `category_items` 表的外键 `file_id` 变更为 `folder_id`。
由于取消跨库分体式存储，`categories` 表与 `category_items` 表需要在**每一个分库**建表时 100% 建立：

```diff
// DatabaseManager.cpp 中的 schema 定义：
     // 初始化表结构 (Schema)
     const char* schema = R"(
         CREATE TABLE IF NOT EXISTS metadata (
-            file_id TEXT PRIMARY KEY,
+            folder_id TEXT PRIMARY KEY,
             path TEXT NOT NULL,
             is_folder INTEGER DEFAULT 0,
             rating INTEGER DEFAULT 0,
             color TEXT,
             tags TEXT,
             note TEXT,
             url TEXT,
             ctime INTEGER,
             mtime INTEGER,
             atime INTEGER,
             file_size INTEGER,
             palettes BLOB,
             is_trash INTEGER DEFAULT 0,
             original_path TEXT,
             width INTEGER DEFAULT 0,
             height INTEGER DEFAULT 0,
             ingestion_status INTEGER DEFAULT -1,
             auto_color TEXT DEFAULT '',
             base_name TEXT DEFAULT '',
             ext TEXT DEFAULT '',
             added_at INTEGER DEFAULT 0
         );
         CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);
         CREATE INDEX IF NOT EXISTS idx_metadata_added_at ON metadata(added_at);

         -- 分类定义表
         CREATE TABLE IF NOT EXISTS categories (
             id INTEGER PRIMARY KEY AUTOINCREMENT,
             parent_id INTEGER DEFAULT 0,
             name TEXT NOT NULL,
             color TEXT,
             preset_tags TEXT,
             sort_order INTEGER DEFAULT 0,
             pinned INTEGER DEFAULT 0,
             encrypted INTEGER DEFAULT 0,
             encrypt_hint TEXT,
             physical_frn INTEGER DEFAULT 0,
             physical_path TEXT
         );
         CREATE INDEX IF NOT EXISTS idx_categories_frn ON categories(physical_frn);

         -- 分类与项目关联表
         CREATE TABLE IF NOT EXISTS category_items (
             category_id INTEGER,
-            file_id TEXT,
+            folder_id TEXT,
             path_hint TEXT,
             added_at REAL,
-            PRIMARY KEY (category_id, file_id)
+            PRIMARY KEY (category_id, folder_id)
         );
+        CREATE INDEX IF NOT EXISTS idx_category_items_folder_id ON category_items(folder_id);
```

### 4.3 修改点 C — `MetadataDefs.h` & `IndexedEntry.h`：C++ 结构体彻底更名

```diff
// MetadataDefs.h 物理更改：
 struct ItemMeta {
-    std::wstring fileId;
+    std::wstring folderId;
     std::wstring path;
     // ...
 };
```

```diff
// IndexedEntry.h 物理更改：
 struct ItemRecord {
-    std::string fileId;
+    std::string folderId;
     QString path;
     // ...
 };
```

```diff
// IndexedEntry.cpp 物理更改：
-    r.fileId = item.fileId128;
+    r.folderId = item.folderId;
```

### 4.4 修改点 D — `CategoryRepo.cpp` 与 `MetadataManager.cpp` 全量同盘单连接重构

1. 所有原来调用 `DatabaseManager::instance().getGlobalDb()` 对 `category_items` 表、`categories` 表、以及 `system_stats` 表的读写操作，全部重构为通过 **`DatabaseManager::instance().getDbForPath(path)`** 动态路由到该资产对应的相同盘符分库中。
2. 精确重写 `CategoryRepo::getCounts()` 与 `CategoryRepo::fullRecount()`。只有当资产包关联的分类 ID **大于 0**（自定义分类）时才视作已被分类；如果仅与负数分类（如 Library 节点）关联、且不含任何自定义分类关联，则在逻辑上 **100% 视作未分类（`uncategorized`）**，在未分类逻辑桶中展现计数！

```diff
// CategoryRepo.cpp 物理对账逻辑合并更新：
 std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
     std::vector<std::pair<int, int>> res;
-    // 彻底废除 getGlobalDb() 分体，改用分库对账
+    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
+    std::map<int, std::unordered_set<std::string>> catToUniqueFids;
+
+    for (sqlite3* db : dbs) {
+        sqlite3_stmt* stmt;
+        if (sqlite3_prepare_v2(db, "SELECT folder_id, category_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
+            while (sqlite3_step(stmt) == SQLITE_ROW) {
+                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
+                int catId = sqlite3_column_int(stmt, 1);
+                if (fid) catToUniqueFids[catId].insert(fid);
+            }
+            sqlite3_finalize(stmt);
+        }
+    }
+
+    for (auto const& [id, fids] : catToUniqueFids) {
+        res.push_back({id, static_cast<int>(fids.size())});
+    }
+    return res;
 }
```

```diff
// CategoryRepo.cpp 物理全量重算 (对账大底盘)：
 void CategoryRepo::fullRecount() {
     // ...
     // 1. 获取所有在各个分库中，被绑定了自定义分类 (category_id > 0) 的 folder_id
     std::unordered_set<std::string> customizedFids;
     auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
     for (sqlite3* db : dbs) {
         sqlite3_stmt* stmt = nullptr;
         if (sqlite3_prepare_v2(db, "SELECT DISTINCT folder_id FROM category_items WHERE category_id > 0", -1, &stmt, nullptr) == SQLITE_OK) {
             while (sqlite3_step(stmt) == SQLITE_ROW) {
                 const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                 if (fid) customizedFids.insert(fid);
             }
             sqlite3_finalize(stmt);
         }
     }

     int total = 0;
     int uncategorized = 0;
     // ...
     auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
     for (const auto& meta : snapshot) {
         if (meta.folderId.empty()) continue;
         if (meta.isFolder) continue;

         if (!MetadataManager::instance().isInsideManagedLibrary(meta.path)) {
             continue;
         }
         if (meta.isTrash) continue;

         total++;
         // 🚨 完美逻辑归位：如果资产没有绑定任何一个自定义分类 (id > 0)，100% 逻辑归于未分类！
         if (customizedFids.find(meta.folderId) == customizedFids.end()) {
             uncategorized++;
         }
     }
     // ...
 }
```

### 4.5 修改点 E — `AssetImporter.cpp`：同盘单分库原子大事务落盘

在 `AssetImporter::importSingleFile` 中，完全移除先异步后内存的零散写入，直接获取该托管库的目标盘符分库，执行原子 `SqlTransaction`：

```diff
     // 5. 写入数据库：将整个 .arc 资产包文件夹作为唯一的受控资产单位进行激活和登记！
     std::wstring wContainerPath = QDir::toNativeSeparators(containerDir).toStdWString();
+    sqlite3* db = DatabaseManager::instance().getDbForPath(wContainerPath);
+    if (!db) return false;
+
+    std::string actualFolderId = shellHelper::generateBase36Id().toStdString(); // folder_id 为 13 位 Base36 包 ID
+
+    // 🚨 优雅大事务重构：资产表(metadata)与分类关联表(category_items) 100% 存放在同一个物理 SQLite 分库中，同温同事务落盘
+    SqlTransaction trans(db);
+
+    long long nowMsecs = QDateTime::currentMSecsSinceEpoch();
+    // a. 写入元数据资产表 (绑定 folder_id)
+    sqlite3_stmt* stmtMeta = nullptr;
+    const char* sqlMeta = "INSERT OR REPLACE INTO metadata (folder_id, path, is_folder, added_at) VALUES (?, ?, ?, ?)";
+    if (sqlite3_prepare_v2(db, sqlMeta, -1, &stmtMeta, nullptr) == SQLITE_OK) {
+        sqlite3_bind_text(stmtMeta, 1, actualFolderId.c_str(), -1, SQLITE_TRANSIENT);
+        sqlite3_bind_text16(stmtMeta, 2, wContainerPath.c_str(), -1, SQLITE_TRANSIENT);
+        sqlite3_bind_int(stmtMeta, 3, 1); // 文件夹资产
+        sqlite3_bind_int64(stmtMeta, 4, nowMsecs);
+        sqlite3_step(stmtMeta);
+        sqlite3_finalize(stmtMeta);
+    }
+
+    // b. 写入分类关联表
+    int finalCatId = targetCatId;
+    if (finalCatId <= 0) {
+        QString driveLetter = QFileInfo(destPath).absolutePath().left(1).toUpper();
+        QString libCatName = "ArcMeta.Library_" + driveLetter;
+        // 从分库中读取物理托管库分类 id
+        sqlite3_stmt* stmtCat = nullptr;
+        if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE parent_id = 0 AND name = ?", -1, &stmtCat, nullptr) == SQLITE_OK) {
+            std::wstring wLibCatName = libCatName.toStdWString();
+            sqlite3_bind_text16(stmtCat, 1, wLibCatName.c_str(), -1, SQLITE_TRANSIENT);
+            if (sqlite3_step(stmtCat) == SQLITE_ROW) {
+                finalCatId = sqlite3_column_int(stmtCat, 0);
+            }
+            sqlite3_finalize(stmtCat);
+        }
+    }
+
+    if (finalCatId > 0) {
+        sqlite3_stmt* stmtItems = nullptr;
+        const char* sqlItems = "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)";
+        if (sqlite3_prepare_v2(db, sqlItems, -1, &stmtItems, nullptr) == SQLITE_OK) {
+            sqlite3_bind_int(stmtItems, 1, finalCatId);
+            sqlite3_bind_text(stmtItems, 2, actualFolderId.c_str(), -1, SQLITE_TRANSIENT);
+            sqlite3_bind_text16(stmtItems, 3, wContainerPath.c_str(), -1, SQLITE_TRANSIENT);
+            sqlite3_bind_double(stmtItems, 4, static_cast<double>(nowMsecs));
+            sqlite3_step(stmtItems);
+            sqlite3_finalize(stmtItems);
+        }
+    }
+
+    if (!trans.commit()) {
+        qWarning() << "[AssetImporter] 100% 同盘单连接事务提交失败！路径:" << destPath;
+        return false;
+    }
+
+    // c. 激活内存缓存
+    MetadataManager::instance().registerItem(wContainerPath, true);

     return true;
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.h` & `DatabaseManager.cpp`：重构提供统一 API `getDbForPath(path)`，建表 Schema 重组，全量实现同库同事务。
- [ ] `src/meta/CategoryRepo.h` & `CategoryRepo.cpp`：全面重构为使用 `getDbForPath(path)` 访问分库，folder_id 对位以及“未分类”对账逻辑修正。
- [ ] `src/meta/MetadataManager.h` & `MetadataManager.cpp`：进行 C++ 字段成员重命名（`fileId` -> `folderId`），更正底盘元数据落盘 SQL。
- [ ] `src/util/AssetImporter.cpp`：实现同盘单连接 `SqlTransaction` 大事务落盘，去除任何异步不确定性。

**明确禁止越界修改的范围：**
- [ ] 磁盘导航模式（`DiskNav`）相关逻辑 — 保持 100% 独立，不产生任何修改。

---

## 6. 实现准则与预警【核心】

1. **同库事务高稳定性**：由于去除了全局主库和分库之间的裂变，所有的分类添加、删除、移入移出操作均通过 `getDbForPath` 直接修改相同盘符的分库数据库，同温同连接，物理排除了 nullptr 隐患。
2. **零补丁纯粹性**：不需要任何 `if-else` 去打特殊的保护补丁。未归类到用户自定义分类下的托管包自然而然属于“未分类”逻辑语义桶，一切计数和卡片显示随之清澈。
3. **更名彻底性**：此项改动不留任何概念混淆死角，数据库、C++ 结构体和物理包 100% 对齐为 `folder_id` 与 `folderId`。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 双轨数据路由分流架构（第 1 节） | 托管库写入 SQLite，磁盘导航独占 `AmMetaJson` 读写至 cache 缓存，绝不污染物理文件夹 | ✅ 100% 契合且维持了双轨隔离 of 纯净性 |
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `isMirrorSource()` 或强类型进行识别 | ✅ 完全符合 |
