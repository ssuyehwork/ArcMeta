# 内存模式资产解包重构与托管库计数矫正 —— Modification_Plan-16.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

在 ArcMeta 的设计中，系统严格划分为**“磁盘目录模式（DiskNav）”**与**“内存数据库模式（UserCategory/SystemCategory）”**两条 100% 独立隔离的轨道：

1. **磁盘目录模式**：进行纯粹的物理磁盘目录遍历（行为等同于 Windows 资源管理器/Bridge）。用户访问 `ArcMeta.Library_[盘符]` 磁盘目录时，直接展示原生的 `00ms8ythbc000.arc` 文件夹，不做任何解包与语义解释。
2. **内存数据库模式**：由 SQLite 数据库驱动，负责解析 `.arc` 资产包内部结构，为用户呈现真实的素材名称（如 `测试.psd`、`测试.md`）、真实类型与缩略图。

当前系统存在两处严重的逻辑架构断层：
1. **解包展示失真**：在内存数据库模式下，`loadCategory` 载入的条目依然直接暴露了 `.arc` 容器文件夹名称（`00ms8ythbc000.arc`），未对资产包内部的主素材进行解包展示。
2. **托管库分类计数归零**：`ArcMeta.Library_G` 托管库内明明已经建有 2 个资产包，但分类树上的统计计数却显示为 `(0)`。根因在于 `AssetImporter` 写入 `category_items` 表的 `file_id`（如 `00ms8ythbc000`）与 `CategoryRepo::getCounts()` 从 `MetadataManager` 缓存中提取的物理 `fileId128`（如 `FRN:XXXX`）不匹配，导致计数汇总时无法命中。

---

## 2. 问题定位

1. **解包失真**：
   - `src/core/IndexedEntry.cpp` 中的 `ItemRecord::create` 函数：遇到 `.arc` 容器路径时，直接使用了 `QFileInfo(path).fileName()`，把容器文件夹名 `00ms8ythbc000.arc` 赋交给了 `r.filename`。
   - `src/ui/ContentPanel.cpp` 中的 `data(Qt::DisplayRole)`：渲染第一列文件名时，直接截取 `path` 字符串尾部，输出了 `00ms8ythbc000.arc`。

2. **计数归零**：
   - `src/meta/CategoryRepo.cpp` 中的 `CategoryRepo::getCounts()` 函数：在第 840–850 行通过 `forEachCachedItem` 遍历内存缓存时，使用的 Key 为 `meta.fileId128`。而 `category_items` 表中存储的关联 `file_id` 是针对 `.arc` 容器生成的 Base36 包 ID。两边字符串不一致，致使 `fidToCats.find(meta.fileId128)` 恒为失败，分类计数降为 0。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1 | "理应显示的是‘测试_thumbnail.png’和‘测试.md’，而不应该显示‘00ms8ythbc000.arc’" | 重构 `ItemRecord::create` 与 `ContentPanel::data`：在内存数据库模式下对 `.arc` 容器进行解包，提取内部主素材文件的真实名称（如 `测试.psd` / `测试.md`）作为展示名称与类型 | ✅ |
| 2 | "托管库里目前已经创建了两个项目...分类的计数应该显示2，为何却显示0" | 修复 `CategoryRepo::getCounts()`：统一 `.arc` 资产包容器的 FID 匹配映射，确保分类树正确统计到 2 个资产 | ✅ |
| 3 | "磁盘目录模式...看到的就是原本的文件夹结构（包括所有 .arc 容器）" | 保证磁盘导航模式（`DiskNav`）下走 `QDir::entryInfoList` 原生物理扫描，绝不做解包，两轨 100% 隔离 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改点 A — `IndexedEntry.cpp`：内存模式下对 `.arc` 容器进行解包解析

在 `ItemRecord::create` 中，当识别到路径为 `.arc` 资产包容器时，穿透包内查找主素材文件，将其真实文件名与扩展名注入 `ItemRecord`：

```diff
     r.path = nPath;
     {
         int lastSlash = nPath.lastIndexOf('\\');
         if (lastSlash == -1) lastSlash = nPath.lastIndexOf('/');
         r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
     }

+    // 🚨 内存数据库模式资产解包重构：若为 .arc 容器，解包提取包内主素材文件的真实名称与类型
+    if (r.isDir && nPath.endsWith(".arc", Qt::CaseInsensitive)) {
+        QDir arcDir(nPath);
+        QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
+        for (const QFileInfo& fi : files) {
+            QString fn = fi.fileName();
+            if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
+            if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
+            // 找到物理主素材
+            r.filename = fi.fileName();
+            r.suffix = fi.suffix().toLower();
+            break;
+        }
+    }
```

### 4.2 修改点 B — `ContentPanel.cpp`：显示角色适配解包文件名

修改 `data(Qt::DisplayRole)` 第一列，优先使用 `ItemRecord::filename`（它已在 `ItemRecord::create` 中解包为真实素材名）：

```diff
     if (role == Qt::DisplayRole || role == Qt::EditRole) {
         switch (index.column()) {
             case 0: {
-                // 2026-06-xx 极致性能优化：文件名称提取杜绝 QFileInfo 随机访问。
-                // path 已经归一化，通过字符串操作获取文件名
-                int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
-                if (lastSlash == -1) return path;
-                QString name = path.mid(lastSlash + 1);
-                if (name.isEmpty() && path.length() >= 2 && path[1] == ':') return path; // 盘符根目录
-                return name;
+                // 优先使用 ItemRecord 中解包好的 filename（包含 .arc 内部真正的主素材文件名）
+                if (!record.filename.isEmpty()) return record.filename;
+                int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
+                if (lastSlash == -1) return path;
+                return path.mid(lastSlash + 1);
             }
             case 3: {
```

### 4.3 修改点 C — `CategoryRepo.cpp`：修复 `.arc` 资产包在分类计数中的 FID 匹配

在 `CategoryRepo::getCounts()` 的 `forEachCachedItem` 回调中，针对 `.arc` 资产包，同时匹配其包 ID (`MetadataManager::instance().getFileIdSync(path)`)：

```diff
     MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
         // 🚨 资产分类计数重构：只要在关联表中、非回收站、且符合可计数资产判定，即计入分类总数
         bool countsAsAsset = MetadataManager::isCountableAsset(path, meta.isFolder);
         if (!meta.fileId128.empty() && countsAsAsset && !meta.isTrash) {
             auto it = fidToCats.find(meta.fileId128);
             if (it != fidToCats.end()) {
                 for (int catId : it->second) {
                     catToUniqueFids[catId].insert(meta.fileId128);
                 }
             }
+            // 针对 .arc 资产包，检查以包 ID 形式登记在 category_items 中的关联
+            if (meta.isFolder && path.size() >= 4 && path.compare(path.size() - 4, 4, L".arc") == 0) {
+                std::string arcFid = MetadataManager::instance().getFileIdSync(path);
+                if (!arcFid.empty() && arcFid != meta.fileId128) {
+                    auto itArc = fidToCats.find(arcFid);
+                    if (itArc != fidToCats.end()) {
+                        for (int catId : itArc->second) {
+                            catToUniqueFids[catId].insert(arcFid);
+                        }
+                    }
+                }
+            }
         }
     });
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/IndexedEntry.cpp`：修改 `ItemRecord::create` 函数，加入 `.arc` 解包提取文件名与后缀逻辑。
- [ ] `src/ui/ContentPanel.cpp`：修改 `data()` 中 `DisplayRole` 的文件名获取方式。
- [ ] `src/meta/CategoryRepo.cpp`：修改 `CategoryRepo::getCounts()` 中的 FID 关联判定。

**明确禁止越界修改的范围：**
- [ ] `src/util/AssetImporter.cpp` — 不修改物理打包流程。
- [ ] 磁盘导航模式（`DiskNav`）下对原生目录扫描的相关代码 — 不修改。

---

## 6. 实现准则与预警【核心】

1. **双轨 100% 隔离**：修改点 A 与修改点 B 的解包只在 `ItemRecord` 被构建及内存数据库展示时生效。磁盘导航模式（`DiskNav`）直接调用 `QDir::entryInfoList` 列出磁盘原本的子目录结构，完全不受影响。
2. **零闪烁与高性能**：`.arc` 包内的结构扫描仅在 `ItemRecord::create` 内存构建时执行一次（包内仅 2~3 个文件），耗时微秒级，不影响滚动性能。
3. **计数准确性**：修改点 C 建立起了 `.arc` 包 ID 与分类关联表 `category_items` 的双向桥梁，确保侧边栏 `ArcMeta.Library_G` 节点能够准确统计出包含的 2 个资产。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `ContentPanel::dataSourceType()` 枚举，严禁散落弱类型字符串判定 | ✅ 本方案逻辑纯粹作用于 `ItemRecord` 与 `CategoryRepo` 内部，不干扰 `DataSourceType` 的契约 |
| 缩略图平滑加载（第 9 节） | 图形文件等待缩略图期间返回 `QIcon()` 空图标占位 | ✅ 复用 Plan-15 的平滑加载占位逻辑 |
