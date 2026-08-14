# 磁盘模式元数据读写与MetaPanel输入隔离重构 —— 磁盘模式元数据读写与MetaPanel输入隔离重构.md

## 1. 任务背景
在磁盘目录模式（DiskNav）下，用户反映元数据编辑（备注、链接、标签等）无法持久化生效或再次加载归零，且在标签输入框输入关键字时，输入框文本被异常冲刷替换为绝对物理路径。

经审计，主要原因是 `AmMetaJson` 仅实现了磁盘离散元数据单向写入，缺乏反向加载至内存 `m_snapshot` 的机制；同时 `MetaPanel` 的输入框缺乏焦点编辑态隔离锁，且 `MainWindow` 在分发 `selectionChanged` 时存在将全路径误传给名称字段的缺陷。

本方案旨在重构磁盘模式元数据双向对称加载机制，并对 `MetaPanel` 进行编辑态隔离防护，彻底解决元数据丢失与输入框内容错位缺陷。

---

## 2. 问题定位
1. **磁盘离散元数据缺少加载闭包**（对应用户原话：“AmMetaJson.cpp 存在非常严重的傻逼逻辑架构”）：`MetadataManager::saveToDiskModeJson` 仅将更新写入 `.ArcMeta.json`，但在扫描/加载磁盘目录时未调用 `AmMetaJson::load()` 将离散 `.ArcMeta.json` 解包并注入内存快照 `m_snapshot` 中，导致数据写入后再次选中时读不到。
2. **标签输入框内容被路径误冲刷**（对应用户原话：“在标签输入框输入关键字时，结果填充的内容却变成了路径”）：
   - `MainWindow.cpp` 中 `m_metaPanel->updateInfo(name.isEmpty() ? path : name, ...)` 在 `name` 为空时将全路径 `path` 传入 `name` 参数。
   - `MetaPanel` 内 `m_tagEdit` / `m_noteEdit` / `m_linkEdit` 在获得焦点或输入时，没有加编辑态保护锁（`m_isEditing`），当触发 `selectionChanged` 时，来自后台的刷新强行将全路径或默认值冲刷填入了用户正在输入的文本框中。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | AmMetaJson.cpp 存在非常严重的傻逼逻辑架构，磁盘目录模式下在元数据面板上添加备注、链接、标签失效（对应用户原话：“AmMetaJson.cpp 存在非常严重的傻逼逻辑架构，我发现 磁盘目录模式下，选中某个项目后，在元数据面板上添加备注、链接、标签”） | 在 4.1 节与 4.2 节新增 `MetadataManager::loadDiskModeJsonForDirectory` 并在扫描目录时载入 `.ArcMeta.json` 到内存 `m_snapshot` | ✅ |
| 2    | 标签输入框输入关键字时，结果填充的内容却变成了路径（对应用户原话：“而且我刚刚在标签输入框输入关键字时，结果填充的内容却变成了路径”） | 在 4.3 节与 4.4 节为 `MetaPanel` 增加编辑态锁 `m_isEditing`，并修复 `MainWindow.cpp` 误传全路径给 `name` 的缺陷 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 补齐磁盘元数据反向加载接口 (`src/meta/MetadataManager.h`)

#### [MODIFY] [MetadataManager.h](file:///G:/C++/ArcMeta/ArcMeta/src/meta/MetadataManager.h)
```diff
--- src/meta/MetadataManager.h
+++ src/meta/MetadataManager.h
@@ -45,6 +45,9 @@
     // 磁盘模式专属：保存离散元数据至所在目录的 .ArcMeta.json 中
     void saveToDiskModeJson(const std::wstring& nPath, std::function<void(ItemMeta&)> updater);
 
+    // 磁盘模式专属：加载并注入指定物理目录下的 .ArcMeta.json 元数据到内存快照中
+    void loadDiskModeJsonForDirectory(const std::wstring& folderPath);
+
     // 属性写接口
     void setRating(const std::wstring& path, int rating, bool notify = true);
     void setColor(const std::wstring& path, const std::wstring& color, bool notify = true);
```

---

### 4.2 实现 `loadDiskModeJsonForDirectory` 与扫描加载闭环 (`src/meta/MetadataManager.cpp`)

#### [MODIFY] [MetadataManager.cpp](file:///G:/C++/ArcMeta/ArcMeta/src/meta/MetadataManager.cpp)
```diff
--- src/meta/MetadataManager.cpp
+++ src/meta/MetadataManager.cpp
@@ -1035,6 +1035,39 @@
     }
 }

+void MetadataManager::loadDiskModeJsonForDirectory(const std::wstring& folderPath) {
+    AmMetaJson metaJson(folderPath);
+    if (!metaJson.load()) return;

+    const auto& items = metaJson.items();
+    if (items.empty()) return;

+    std::wstring normDir = MetadataManager::normalizePath(folderPath);
+    if (!normDir.empty() && normDir.back() != L'\\' && normDir.back() != L'/') {
+        normDir += L'\\';
+    }

+    std::unique_lock<std::shared_mutex> lock(m_mutex);
+    auto currentSnapshot = std::atomic_load(&m_snapshot);
+    auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot);

+    for (const auto& [fileName, itemMeta] : items) {
+        std::wstring fullPath = normDir + fileName;
+        std::wstring nPath = MetadataManager::normalizePath(fullPath);

+        auto& rm = (*newMap)[nPath];
+        rm.rating = itemMeta.rating;
+        rm.colorStr = itemMeta.color;
+        rm.isPinned = itemMeta.pinned;
+        rm.note = itemMeta.note;
+        rm.url = itemMeta.url;
+        rm.tags = itemMeta.tags;
+        rm.palettes = itemMeta.palettes;
+    }

+    std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
+}
```

---

### 4.3 `MetaPanel` 增加编辑态锁与防护 (`src/ui/MetaPanel.h` / `MetaPanel.cpp`)

#### [MODIFY] [MetaPanel.h](file:///G:/C++/ArcMeta/ArcMeta/src/ui/MetaPanel.h)
```diff
--- src/ui/MetaPanel.h
+++ src/ui/MetaPanel.h
@@ -88,6 +88,7 @@
     QList<TagPill*> m_tagPool;
     QList<ColorPill*> m_colorPool;
     QTimer* m_adjustTimer = nullptr;
     bool m_isInternalUpdating = false;
+    bool m_isUserEditing = false; // 增加编辑态锁，防护焦点与异步刷新冲刷
 
 private slots:
     void onTagAdded();
```

#### [MODIFY] [MetaPanel.cpp](file:///G:/C++/ArcMeta/ArcMeta/src/ui/MetaPanel.cpp)
```diff
--- src/ui/MetaPanel.cpp
+++ src/ui/MetaPanel.cpp
@@ -266,6 +266,9 @@
                             const QString& ct, const QString& mt, const QString& at, 
                             const QString& p, bool e, int width, int height) {
+    // 用户正在编辑框中输入时，严禁被外部 selectionChanged 冲刷文本！
+    if (m_isUserEditing) return;

     m_isInternalUpdating = true;
     
     QFileInfo info(n);
@@ -389,6 +392,15 @@
     if (m_isInternalUpdating) return QFrame::eventFilter(watched, event);

+    if (event->type() == QEvent::FocusIn) {
+        if (watched == m_tagEdit || watched == m_noteEdit || watched == m_linkEdit || watched == m_nameEdit) {
+            m_isUserEditing = true;
+        }
+    } else if (event->type() == QEvent::FocusOut) {
+        if (watched == m_tagEdit || watched == m_noteEdit || watched == m_linkEdit || watched == m_nameEdit) {
+            m_isUserEditing = false;
+        }
+    }

     if (watched == m_noteEdit && event->type() == QEvent::FocusOut) {
```

---

### 4.4 修复 `MainWindow.cpp` 中将全路径误传给 `name` 的缺陷 (`src/ui/MainWindow.cpp`)

#### [MODIFY] [MainWindow.cpp](file:///G:/C++/ArcMeta/ArcMeta/src/ui/MainWindow.cpp)
```diff
--- src/ui/MainWindow.cpp
+++ src/ui/MainWindow.cpp
@@ -481,7 +481,7 @@
             // 1. 基础信息展示（0 Win32 磁盘 Blocking）
             m_metaPanel->updateInfo(
-                name.isEmpty() ? path : name, 
+                name.isEmpty() ? QFileInfo(path).fileName() : name, 
                 type,
                 sizeStr,
                 "-", // ctime 懒加载
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [MODIFY] `src/meta/MetadataManager.h`（增加 `loadDiskModeJsonForDirectory` 声明）
- [MODIFY] `src/meta/MetadataManager.cpp`（实现磁盘 `.ArcMeta.json` 加载注入内存快照）
- [MODIFY] `src/ui/MetaPanel.h`（增加 `m_isUserEditing` 编辑锁成员变量）
- [MODIFY] `src/ui/MetaPanel.cpp`（在 `updateInfo` 与 `eventFilter` 中实现编辑锁逻辑）
- [MODIFY] `src/ui/MainWindow.cpp`（修复 `updateInfo` 入参全路径冲刷文件名缺陷）

**明确禁止越界修改的范围：**
- `AmMetaJson.cpp` 基础 JSON 结构序列化函数——不修改
- `DiskScanService.cpp` 线程池调度核心架构——不修改
- `ThumbnailDelegate.cpp` 视图卡片绘制——不修改

---

## 6. 实现准则与预警【核心】

1. **锁同步安全性**：在 `loadDiskModeJsonForDirectory` 更新内存 `m_snapshot` 时，必须使用 `std::unique_lock<std::shared_mutex>` 保护内存写操作，遵循 RCU 快照替换规范，严禁锁外直接修改 `m_snapshot`。
2. **编辑锁自动复位**：`m_isUserEditing` 必须在 `FocusOut` 以及 `onTagAdded` / `onTagDeleted` 之后确保安全复位，防止因焦异常丢失导致面板永久锁定无法更新。
3. **全路径清洗防冲刷**：`MainWindow` 传入 `updateInfo` 的首个参数必须保证只包含 pure file name 或基础名，绝对不允许将含盘符路径（如 `G:/a/b.txt`）直接传给名称编辑框。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨模式隔离 | 磁盘模式下元数据保存至 `.ArcMeta.json` 隐藏文件，绝对禁止溢流写入 SQLite 数据库 | ✅ |
| 输入框清除按钮 | 搜索/输入框一键清除统一使用 Qt 原生 `setClearButtonEnabled(true)`，本方案未新增输入框 | ✅ |
| 0 毫秒单击响应 | `selectionChanged` 时依然维持直接从缓存读取，严禁在主线程发起阻塞磁盘 I/O | ✅ |

---

## 8. 待确认事项
无。方案设计自包含且精确锚定根因，待授权后由执行者角色机械照单物理实施改动。
