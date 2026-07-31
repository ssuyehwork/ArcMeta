# 内存模式分类内容面板缩略图穿透显示 —— Modification_Plan-15.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

在内存模式下，用户将素材拖拽导入托管库时，`AssetImporter::importSingleFile` 会为每个素材创建一个 `[ID].arc` 资产包文件夹，并在其内部生成 `*_thumbnail.png` 缩略图。该 `.arc` 文件夹路径本身被注册到 MetadataManager 的 `m_fidToPath` 映射中，作为该素材的唯一物理标识。

当用户点击侧边栏分类时，`loadCategory` 从数据库取出每个素材的 FID，通过 `getPathByFid` 得到 `.arc` 文件夹路径，传入 `ItemRecord::create`，生成 `isDir=true、suffix=""` 的记录。内容面板渲染时，`ext="arc"` 不被识别为图形文件，最终通过 `UiHelper::getFileIcon` 返回文件夹图标，而非包内的 `*_thumbnail.png` 缩略图。

---

## 2. 问题定位

**文件**：`src/ui/ContentPanel.cpp`

**根因链（三段式）**：

| 段 | 位置 | 问题 |
|---|---|---|
| 段一 | `data()` → `DecorationRole`（第 265–278 行） | `ext="arc"` 不满足 `isGraphic`，直接返回文件夹系统图标，不触发占位符逻辑 |
| 段二 | `loadThumbnailsForRows` 后台加载分支（第 680–721 行） | `ext="arc"` 未命中任何分支（svg/ai/isGraphicsFile），`img` 为空，最终缓存的仍是文件夹图标 |
| 段三 | `data()` → `HasThumbnailRole`（第 250–264 行） | `suffix=""` + 无宽高比缓存 → 返回 `false`，ThumbnailDelegate 不绘制图片边框 |

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1 | "内容面板里显示的应该是 icon/缩略图，而不是 00ms73182x000.arc 文件夹" | 修复 `DecorationRole` 返回值：`.arc` 路径等待加载期间返回 `QIcon()`（灰色占位），加载完成后从缓存返回真实缩略图 | ✅ |
| 2 | "只有内存模式才会去使用 .arc" | 所有修改仅在 `ext == "arc" && info.isDir()` 条件下触发，磁盘模式（DiskNav）路径无扩展名 `arc`，完全不受影响 | ✅ |
| 3 | "以 00ms73182x000 作为数据库唯一 ID" | 不修改 FID/路径注册逻辑，`path`（即 `.arc` 文件夹路径）仍作为缓存 Key 和唯一标识 | ✅ |
| 4 | 不修改 AssetImporter / MetadataManager | 方案修改边界严格限定于 `ContentPanel.cpp` 一个文件 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改点 A — `HasThumbnailRole`（第 250–264 行）

在 `.arc` 资产包容器判定加在 `UiHelper::isGraphicsFile` 判定之前：

```diff
     } else if (role == HasThumbnailRole) {
         static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
         if (iconOnlyExts.contains(record.suffix.toLower())) return false;
         if (record.suffix.toLower() == "ai") {
             QString nativePath = QDir::toNativeSeparators(path);
             if (m_aspectRatios.contains(nativePath)) {
                 return m_aspectRatios.value(nativePath) > 0.0;
             }
             return false;
         }
+        // .arc 资产包容器：以宽高比缓存是否已命中为准，加载完成前返回 false，避免虚假边框
+        if (record.isDir && path.endsWith(".arc", Qt::CaseInsensitive)) {
+            QString nativePath = QDir::toNativeSeparators(path);
+            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
+        }
         if (UiHelper::isGraphicsFile(record.suffix)) return true;
         if (record.width > 0 && record.height > 0) return true;
         return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
```

### 4.2 修改点 B — `DecorationRole`（第 265–278 行）

为 `.arc` 容器添加"等待加载返回空图标"逻辑，与图形文件保持一致：

```diff
     } else if (role == Qt::DecorationRole && index.column() == 0) {
         QString cacheKey = path;
         QIcon* cached = m_iconCache.object(cacheKey);
         if (cached) return *cached;

         QFileInfo info(path);
         QString ext = info.suffix().toLower();
         bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";

+        // .arc 资产包容器：包内存在 _thumbnail.png，视同图形文件，等待异步加载时返回空图标占位
+        bool isArcContainer = (ext == "arc" && info.isDir());

-        if (isGraphic) return QIcon();
+        if (isGraphic || isArcContainer) return QIcon();
         return UiHelper::getFileIcon(path, 128);
     }
```

### 4.3 修改点 C — `loadThumbnailsForRows` needLoad 判定（第 643–648 行）

将 `.arc` 容器加入"宽高比缓存缺失时强制拉起加载"的判定范围：

```diff
         bool needLoad = !m_iconCache.contains(cacheKey);
+        bool isArcContainer = rec.isDir && rec.path.endsWith(".arc", Qt::CaseInsensitive);
-        if (UiHelper::isGraphicsFile(rec.suffix) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
+        if ((UiHelper::isGraphicsFile(rec.suffix) || isArcContainer) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
             needLoad = true;
         }
         if (!needLoad) continue;
```

### 4.4 修改点 D — `loadThumbnailsForRows` 后台加载分支（第 717–721 行之后）

在 `cur/ico/ani` 的 `else if` 块之后，追加 `.arc` 分支：

```diff
                     } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                         ar = 1.0;
                         hasThumb = false;
+                    } else if (ext == "arc" && info.isDir()) {
+                        // .arc 资产包容器：穿透进包内，寻找 *_thumbnail.png 作为缩略图
+                        QDir arcDir(path);
+                        QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
+                        if (!thumbFiles.isEmpty()) {
+                            QString thumbPath = path + "/" + thumbFiles.first();
+                            img = QImage(thumbPath);
+                            if (!img.isNull()) {
+                                ar = (double)img.width() / img.height();
+                                hasThumb = true;
+                            }
+                        }
                     }
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp`：仅修改 `ArcMetaVirtualDbModel::data()` 和 `ArcMetaVirtualDbModel::loadThumbnailsForRows()` 两个函数内部的局部逻辑，共四处 diff 块（4.1 / 4.2 / 4.3 / 4.4）

**明确禁止越界修改的范围：**
- [ ] `src/util/AssetImporter.cpp` — 不修改
- [ ] `src/meta/MetadataManager.cpp` — 不修改
- [ ] `src/meta/CategoryRepo.cpp` — 不修改
- [ ] `src/core/IndexedEntry.cpp` — 不修改
- [ ] `loadCategory` 函数 — 不修改
- [ ] 磁盘模式（DiskNav）相关任何逻辑 — 不修改

---

## 6. 实现准则与预警【核心】

1. **头文件**：`QDir`（第 50 行）、`QImage`（已在 SVG 渲染分支使用）、`QFileInfo`（第 49 行）均已在 `ContentPanel.cpp` 中引入，无需新增任何 `#include`。

2. **线程安全**：修改点 4.4 的 `QDir::entryList` 调用位于 `QtConcurrent::run` 后台线程内，与现有 svg/ai/图形文件的加载方式完全一致，无跨线程 UI 访问风险。

3. **缓存一致性**：`.arc` 容器加载完成后，通过现有的 `m_iconCache.insert` 和 `m_aspectRatios[...]` 路径存储结果，`dataChanged` 信号自动触发重绘，与现有图形文件的更新机制完全复用，无需额外信号。

4. **磁盘模式隔离**：所有新增条件均以 `ext == "arc" && info.isDir()` 双重守卫，磁盘模式下普通文件扩展名不会触发此分支，两种模式互不干扰。

5. **`_thumbnail.png` 不存在时的兜底**：若 `.arc` 包内无 `*_thumbnail.png`（如导入时生成失败），`thumbFiles.isEmpty()` 为真，`img` 保持 null，后续走 `icon = UiHelper::getFileIcon(path, 128)` 兜底显示文件夹图标，不会崩溃。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 缩略图平滑加载（第 9 节） | 图形文件等待缩略图期间，`data()` 必须返回空图标 `QIcon()`，由 Delegate 绘制灰色圆角矩形占位背景，消除"系统图标→缩略图"二段式闪烁 | ✅ 修改点 B 对 `.arc` 容器同样返回 `QIcon()`，与规范一致 |
| UI 异步加载防闪烁（第 8 节） | 异步扫描前禁止 `clear()`，保留旧数据至 `setRecords` 原子替换 | ✅ 本方案不触碰 `loadCategory` 和 `setRecords` 逻辑 |
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `ContentPanel::dataSourceType()` 枚举，严禁散落弱类型字符串判定 | ✅ 本方案以物理路径特征 `ext=="arc" && isDir` 做条件判定，属于文件系统属性判断，不属于数据源类型判定，不违反该规范 |
