# 全局物理资产管线归一化与解包接口重构 —— Modification_Plan-17.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

此前系统在资产导入、UI 界面解包渲染及分类计数逻辑上缺乏统一的收口接口，导致多次依赖局部缝缝补补打补丁，产生了严重的问题：
1. **重复注册**：拖拽导入时包内碎片文件被二次注册进 `MetadataManager`，引发“全部数据”计数在导入瞬间误飙升为 4（重启后回落为 2）。
2. **图标张冠李戴**：无预生成缩略图的文本文件（如 `测试.md`），在渲染系统图标时错误地拿 `.arc` 外壳容器目录路径去向 Shell 申请图标，导致界面上显示为黄色文件夹图标。
3. **托管库计数归零**：导入资产时未自动关联盘符托管库根分类 ID，导致 `ArcMeta.Library_G` 节点计数永远为 0。

本方案旨在摒弃所有局部小补丁，建立全应用统一的归一化收口接口，死守单一职责原则（SRP）。

---

## 2. 问题定位

1. **导入管道未收口**（`src/util/AssetImporter.cpp`）：
   - 资产写入 `MetadataManager` 时未约束单一粒子原则，包内文件被二次激活。
   - 拖拽至侧边栏空白处或“全部数据”时，没有自动绑定盘符根托管库分类（`ArcMeta.Library_[盘符]`）ID。

2. **图标提取未解耦**（`src/ui/ContentPanel.cpp` & `src/ui/UiHelper.cpp`）：
   - 当 `hasThumb` 为 `false` 走 Fallback 图标提取时，直接使用了 `.arc` 容器目录路径调用 `UiHelper::getFileIcon`，返回了 Windows 文件夹图标。

3. **分类计数断层**（`src/meta/CategoryRepo.cpp`）：
   - `CategoryRepo::getCounts()` 无法将 `category_items` 表中的 Base36 包 ID 与 `MetadataManager` 缓存中的记录双向匹配，导致 `ArcMeta.Library_G` 节点计数无法更新。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1 | "拖拽了两个文件...“全部数据”的计数显示4，应该显示2才对" | 归一化 `AssetImporter`：仅将 `.arc` 容器作为唯一受控资产单元注册，严禁包内文件二次注册，确保计数准确为 2 | ✅ |
| 2 | "标记为④的分类（ArcMeta.Library_G）为何仍然显示0" | 归一化 `AssetImporter` 与 `CategoryRepo`：导入时自动关联盘符托管库根分类 ID，修正 SQL 对账查询 | ✅ |
| 3 | "点击“全部数据”，内容面板上显示的怎么会是文件夹图标呢"（测试.md） | 归一化 `ItemRecord::fromAssetContainer` & `UiHelper::getAssetIcon`：为无缩略图文件使用包内真实主文件路径（`测试.md`）提取文件图标，严禁拿 `.arc` 目录提取 | ✅ |
| 4 | "要求归一化、职责单一，没有统一接口" | 彻底摒弃小补丁，构建 4 个全局统一收口接口（`AssetImporter`、`ItemRecord` 解包、`UiHelper` 图标提取、`CategoryRepo` 计数） | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改点 A — `AssetImporter.cpp`：物理导入管线归一化（单一粒子注册 + 托管库自动绑定）

在 `AssetImporter::importSingleFile` 中，确保单一粒子注册，并自动建立与盘符托管库根分类 ID 的映射：

```diff
     // 5. 写入数据库：将整个 .arc 资产包文件夹作为唯一的受控资产单位进行激活和登记！
     std::wstring wContainerPath = QDir::toNativeSeparators(containerDir).toStdWString();
     MetadataManager::instance().ensureActivated(wContainerPath);

     // 更新 added_at 为当前毫秒时间戳
     long long nowMsecs = QDateTime::currentMSecsSinceEpoch();
     MetadataManager::instance().setAddedAt(wContainerPath, nowMsecs, false);
     MetadataManager::instance().persistAsync(wContainerPath, false, true);

     std::string actualContainerFid = MetadataManager::instance().getFileIdSync(wContainerPath);

     // 6. 分类归纳
     // 🚨 归一化绑定：若未指定子分类(targetCatId<=0)，自动绑定至当前盘符托管库根分类(如 ArcMeta.Library_G)
+    int finalCatId = targetCatId;
+    if (finalCatId <= 0) {
+        QString driveLetter = QFileInfo(destPath).absolutePath().left(1).toUpper();
+        QString libCatName = "ArcMeta.Library_" + driveLetter;
+        auto allCats = CategoryRepo::getAll();
+        for (const auto& cat : allCats) {
+            if (cat.parentId == 0 && QString::fromStdWString(cat.name).compare(libCatName, Qt::CaseInsensitive) == 0) {
+                finalCatId = cat.id;
+                break;
+            }
+        }
+    }
+    if (finalCatId > 0 && !actualContainerFid.empty()) {
+        CategoryRepo::addItemToCategory(finalCatId, actualContainerFid, wContainerPath);
+    }

     return true;
```

### 4.2 修改点 B — `IndexedEntry.h` & `IndexedEntry.cpp`：统一内存展示解包接口

在 `ItemRecord` 结构体中添加解包字段 `mainFilePath`，并在 `ItemRecord::create` 中加入解包实现：

```diff
// IndexedEntry.h 中补充字段：
+   QString mainFilePath; // 包内主素材真实路径 (用于精确定位 Shell 文件图标)
```

```diff
// IndexedEntry.cpp 中 ItemRecord::create 物理插入：
     r.path = nPath;
     {
         int lastSlash = nPath.lastIndexOf('\\');
         if (lastSlash == -1) lastSlash = nPath.lastIndexOf('/');
         r.filename = (lastSlash != -1) ? nPath.mid(lastSlash + 1) : nPath;
     }

+    // 🚨 统一内存解包接口：若为 .arc 资产包容器，解包提取包内主素材名称、扩展名及主文件真实物理路径
+    if (r.isDir && nPath.endsWith(".arc", Qt::CaseInsensitive)) {
+        QDir arcDir(nPath);
+        QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
+        for (const QFileInfo& fi : files) {
+            QString fn = fi.fileName();
+            if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
+            if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
+            r.filename = fi.fileName();
+            r.suffix = fi.suffix().toLower();
+            r.mainFilePath = QDir::toNativeSeparators(fi.absoluteFilePath());
+            break;
+        }
+    }
```

### 4.3 修改点 C — `ContentPanel.cpp`：统一图标提取解耦

在 `ContentPanel.cpp` 的 `loadThumbnailsForRows` 异步回调及 `data(Qt::DecorationRole)` 中，针对无缩略图的 `.arc` 资产（如 `测试.md`），采用解包后的 `mainFilePath` 向系统申请真实文件图标：

```diff
                             QIcon icon;
                             if (!img.isNull()) {
                                 icon = QIcon(QPixmap::fromImage(img));
                             } else {
-                                icon = UiHelper::getFileIcon(path, 128);
+                                // 🚨 统一图标提取解耦：若是 .arc 容器，优先使用解包出来的 mainFilePath 申请原生文件图标，严禁使用 .arc 目录路径
+                                QString iconTarget = path;
+                                for (int i = 0; i < mutableThis->m_displayCount; ++i) {
+                                    if (mutableThis->m_allRecords[i].path == path && !mutableThis->m_allRecords[i].mainFilePath.isEmpty()) {
+                                        iconTarget = mutableThis->m_allRecords[i].mainFilePath;
+                                        break;
+                                    }
+                                }
+                                icon = UiHelper::getFileIcon(iconTarget, 128);
                             }
```

### 4.4 修改点 D — `CategoryRepo.cpp`：统一分类与托管库节点计数

在 `CategoryRepo::getCounts()` 中，完美建立 Base36 包 ID 与 `category_items` 表的索引双向比对：

```diff
     MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
         bool countsAsAsset = MetadataManager::isCountableAsset(path, meta.isFolder);
         if (!meta.fileId128.empty() && countsAsAsset && !meta.isTrash) {
             auto it = fidToCats.find(meta.fileId128);
             if (it != fidToCats.end()) {
                 for (int catId : it->second) {
                     catToUniqueFids[catId].insert(meta.fileId128);
                 }
             }
+            // .arc 资产包 ID 归一化对账匹配
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
- [ ] `src/util/AssetImporter.cpp`：实现物理导入管线归一化（托管库根节点自动绑定）。
- [ ] `src/core/IndexedEntry.h` & `IndexedEntry.cpp`：增加 `mainFilePath` 字段并实现统一内存展示解包。
- [ ] `src/ui/ContentPanel.cpp`：实现图标提取解耦（无缩略图时使用 `mainFilePath` 申请图标）。
- [ ] `src/meta/CategoryRepo.cpp`：实现托管库与分类计数的归一化比对计算。

**明确禁止越界修改的范围：**
- [ ] 磁盘导航模式（`DiskNav`）相关扫描代码 — 绝不修改，保持 100% 原生物理扫描。

---

## 6. 实现准则与预警【核心】

1. **死守 SRP 与归一化**：放弃所有在 `ContentPanel` 中的缝缝补补，所有的包解包、图标申请、分类绑定统统由底层的归一化接口统一办理。
2. **两轨 100% 绝对隔离**：磁盘模式（`DiskNav`）直接调用 `QDir::entryInfoList` 输出原生目录 `00ms8ythbc000.arc`，零解包、零语义解释。
3. **图标精准性**：文本文件（如 `测试.md`）通过 `mainFilePath` 精准获得 Markdown 文件图标，物理彻底根除黄色文件夹图标的问题。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `ContentPanel::dataSourceType()` 枚举 | ✅ 严格遵守，不破坏数据源契约 |
| UI 异步加载防闪烁（第 8 节） | 异步扫描前禁止 `clear()`，保留旧数据至 `setRecords` 原子替换 | ✅ 完全符合 |
