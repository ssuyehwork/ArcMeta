# 磁盘导航模式双轨元数据路由隔离、`AmMetaJson` 独占持久化与 1:1 结构一致对等重构 —— Modification_Plan-5.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在确定了 `ArcMeta.Library_[盘符]` 托管库与 `.arc` 资产包封装架构后，系统形成了 **“托管库模式（内存/资产库）”** 与 **“磁盘导航模式（磁盘模式/物理浏览）”** 明确的双轨机制。

本方案承接自 `Modification_Plan-4.md`（旧编号），根据用户最新关于“**DB 类数据库和 JSON 两者的结构必须 1:1 一致，必须保持高度一致！而且是 1:1 的字段语义对等**”的决策，产出此最新重构设计方案。旨在建立一套强类型判定中枢与 100% 双轨数据路由隔离的同时，保证 DB（SQLite 数据库）与 JSON 文件中 Item 元数据结构的完全一致性，消除功能割裂，保障无损入库与高效协同。

---

## 2. 问题定位
当前系统虽然在 `MetadataDefs.h` 中提供了 `ItemMeta` 结构体，并在 `AmMetaJson.cpp` 中提供了 JSON 的读写逻辑，但相比于数据库的 `metadata` 物理表结构，JSON 存储机制缺漏了：
*   **自适应主色 (`auto_color`)**
*   **添加/导入日期 (`added_at`)**
*   **图像宽度 / 高度 (`width` / `height`)**

这导致了在“磁盘模式”下无法对这些关键元数据进行读取和持久化，从而无法保障在向托管库无损迁移时数据的完整继承（对应用户原话：“DB类数据库和JSON两者的结构必须1:1一致”）。因此需要对 `ItemMeta` 的定义、`AmMetaJson` 转换映射逻辑进行扩容。

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | DB类数据库和JSON两者的结构必须1:1一致，必须保持高度一致！而且是 1:1 的字段语义对等！ | 详见 4.1 节，在 `ItemMeta` 中补齐 `added_at`（`addedAt`）、`width`、`height` 和 `auto_color`（`autoColor`）等字段，并在 `AmMetaJson` 序列化层 100% 对齐。 | ✅ |
| 2 | 怎么去判断数据源才是最靠谱最稳妥的？防止后续开发判断不精准 | 详见 4.2 节，在 `ContentPanel.h` 建立强类型 `DataSourceType` 枚举与 3 个单一权威判定接口。 | ✅ |
| 3 | 内容面板显示来源于磁盘时，拖拽项目默认粘贴到当前文件夹里，不执行 `AssetImporter` | 详见 4.3 节，`onPathsDropped` 根据 `dataSourceType()` 分流：`DiskNav` 走物理粘贴，托管库走 `AssetImporter`。 | ✅ |
| 4 | 磁盘模式下打星级、颜色、备注，执行的应该是 `AmMetaJson.cpp`，只作用于磁盘模式，不可作用于托管库 | 详见 4.4 节，在 `MetadataManager.cpp` 中建立双轨防火墙：`DiskNav` 100% 独占写 `AmMetaJson`，库内写 SQLite。 | ✅ |
| 5 | 磁盘模式下进入物理文件夹时自动读取 `ArcMeta.cache/*.json` 显示打标 | 详见 4.5 节，`loadDirectory` 的 `scanDir` 后台线程自动装载 `AmMetaJson` 填充 `ItemRecord`。 | ✅ |
| 6 | 磁盘模式下重命名物理文件/文件夹，同步迁移 JSON 缓存 | 详见 4.6 节，物理重命名成功后调用 `AmMetaJson::renameItem` / `migrateFolderCache`。 | ✅ |

---

## 4. 详细解决方案

### 4.1 解决 1：在 `MetadataDefs.h` 和 `AmMetaJson.cpp` 中实现 DB 与 JSON 的 1:1 完全对等
*   **扩容 `ItemMeta` 结构体**：
    在 `src/meta/MetadataDefs.h` 的 `struct ItemMeta` 中增加 4 个关键属性字段（对应用户原话：“两者的结构必须1:1一致”）：
    ```cpp
    std::wstring autoColor; // 2026-07-xx 1:1对等：自适应主色
    long long addedAt;      // 2026-07-xx 1:1对等：添加/导入日期 (时间戳)
    int width;              // 2026-07-xx 1:1对等：图像宽度
    int height;             // 2026-07-xx 1:1对等：图像高度
    ```
    并更新构造函数：
    ```cpp
    ItemMeta()
        : type(L"file")
        , rating(0)
        , pinned(false)
        , encrypted(false)
        , ingestionStatus(-1)
        , size(0)
        , creationTime(0)
        , modificationTime(0)
        , accessTime(0)
        , width(0)
        , height(0)
        , addedAt(0)
    {}
    ```
    并更新 `hasUserOperations()`：
    ```cpp
    bool hasUserOperations() const {
        return rating > 0 || !color.empty() || !tags.empty() || pinned ||
               !note.empty() || !url.empty() || encrypted || !fileId128.empty() || !palettes.empty() ||
               !autoColor.empty() || addedAt > 0 || width > 0 || height > 0;
    }
    ```

*   **对齐 `AmMetaJson` 序列化与反序列化对等契约**：
    在 `src/meta/AmMetaJson.cpp` 中：
    1.  `itemToEntry` 扩充对等字段的写入（对应用户原话：“仅物理存储格式稍有不同（SQLite 里数组存为逗号分隔符或 Blob，JSON 里存为原生 QJsonArray）”）：
        ```cpp
        obj.insert("width", meta.width);
        obj.insert("height", meta.height);
        obj.insert("auto_color", toQString(meta.autoColor));
        obj.insert("added_at", meta.addedAt);
        ```
    2.  `entryToItem` 扩充对等字段的提取还原：
        ```cpp
        meta.width = obj.value("width").toInt(0);
        meta.height = obj.value("height").toInt(0);
        meta.autoColor = toStdWString(obj.value("auto_color").toString());
        meta.addedAt = obj.value("added_at").toVariant().toLongLong();
        ```

### 4.2 解决 2：在 `ContentPanel.h` 中建立强类型 `DataSourceType` 数据源契约
*   **定义强类型枚举**：
    在 `src/ui/ContentPanel.h` 中定义 `DataSourceType` 枚举，彻底废除散落的字符串硬编码比对：
    ```cpp
    enum class DataSourceType {
        DiskNav,        // 物理磁盘导航模式 (如 D:\Photos，随点随看，离散 JSON 缓存)
        UserCategory,   // 用户自定义逻辑分类 (如 "商业设计原稿"，ID > 0)
        SystemCategory, // 系统逻辑桶 (全部数据, 未分类, 垃圾桶, 最近访问)
        PathList        // 临时路径列表 (搜索结果, 标签筛选)
    };
    ```
*   **提供 3 个单一权威判定接口**：
    1.  `DataSourceType dataSourceType() const`：返回当前精确的数据源类型。
    2.  `bool isMirrorSource() const`：判断是否处于逻辑/镜像模式（`dataSourceType() != DiskNav`）。
    3.  `bool isManagedContext() const`：判断当前是否处于托管库（`ArcMeta.Library_X`）内部或系统逻辑视图中。

### 4.3 解决 3：在 `ContentPanel.cpp` 解锁磁盘模式编辑权限并建立拖拽分流中枢
*   **解封 `setData` 编辑权限**：
    在 `ArcMetaVirtualDbModel::setData` 中，彻底物理删除原本对非 `isInsideLibrary` 下 RatingRole/ColorRole 抛出“编辑受阻”警告弹窗并返回 false 的拦截代码。允许磁盘模式下自由打标。
*   **拖放分流中枢（`onPathsDropped`）**：
    ```cpp
    void ContentPanel::onPathsDropped(const QStringList& droppedPaths, const QModelIndex& targetIndex) {
        if (droppedPaths.isEmpty()) return;

        if (dataSourceType() == DataSourceType::DiskNav) {
            // 【分流 A：磁盘导航模式】──> 执行标准的操作系统级物理粘贴/复制，绝不调用 AssetImporter！
            QString destDir = m_currentPath; 
            if (targetIndex.isValid() && targetIndex.data(TypeRole).toString() == "folder") {
                destDir = targetIndex.data(PathRole).toString();
            }
            bool isMove = !(QApplication::keyboardModifiers() & Qt::ControlModifier);
            if (ShellHelper::copyOrMoveItems(droppedPaths, destDir, isMove)) {
                loadDirectory(m_currentPath, m_isRecursive);
            }
        } else {
            // 【分流 B：托管库/逻辑分类模式】──> 弹出确认框并调用 AssetImporter 执行 .arc 打包入库
            QString targetLibRoot = ...;
            int targetCatId = ...;
            AssetImporter::importAssets(droppedPaths, targetLibRoot, targetCatId, mode, progressFn);
        }
    }
    ```

### 4.4 解决 4：在 `MetadataManager.cpp` 中加装“双轨元数据防倒灌防火墙”
在 `MetadataManager.cpp` 中重构 `setRating`、`setColor`、`setTags`、`setNote`、`setURL`、`setPinned`：
*   **实现通用的离散 JSON 落盘辅助函数**：
    ```cpp
    void MetadataManager::saveToDiskModeJson(const std::wstring& nPath, std::function<void(ItemMeta&)> updater) {
        QFileInfo info(QString::fromStdWString(nPath));
        AmMetaJson jsonCache(info.absolutePath().toStdWString());
        jsonCache.load();
        updater(jsonCache.items()[info.fileName().toStdWString()]);
        jsonCache.save(); // 物理落盘写进 ArcMeta.cache/*.json，零 SQLite 污染！
    }
    ```
*   **路由分流逻辑**：
    *   `if (isInsideManagedLibrary(nPath))` $\rightarrow$ **100% 独占写 SQLite 数据库 (`persistAsync`)**（对应用户原话：“托管库模式下的操作100%写入SQLite”）；
    *   `else`（磁盘导航模式） $\rightarrow$ **100% 独占写 `AmMetaJson` (`saveToDiskModeJson`)**，绝对不出界触碰 SQLite 数据库！（对应用户原话：“磁盘模式下的打星、设颜色、加标签100%独占调用AmMetaJson.cpp写入ArcMeta.cache/*.json，0%接触或污染SQLite数据库”）。

### 4.5 解决 5：在 `loadDirectory` 物理扫描中自动装载 `AmMetaJson` 高级 JSON 缓存
*   在 `ContentPanel.cpp` 的 `loadDirectory` 后台扫描 Lambda 函数 `scanDir` 中：
    *   在进入每个物理文件夹 `p` 时，实例化并加载 `AmMetaJson jsonCache(p.toStdWString()); jsonCache.load();`；
    *   遍历每一个物理文件时，通过其文件名在 `jsonCache.items()` 中快速反查；
    *   若命中，自动将 JSON 中记录的 `rating`、`color`、`pinned`、`note`、`tags`、`url`、`width`、`height`、`autoColor`、`addedAt` 注入对应的 `ItemRecord` 中，实现在未导入数据库的情况下，磁盘扫描出来的瞬间也能精准亮出完整的打标与尺寸属性标记。

### 4.6 解决 6：磁盘模式下的重命名与缓存 Key 同步机制
*   在 `ContentPanel::performBatchRename` 及 `MetadataManager::renameItem` 中：
    *   当重命名成功且处于 `DataSourceType::DiskNav` 模式下时，同步调用 `AmMetaJson::renameItem(folderPath, oldName, newName)`；
    *   当物理文件夹整体重命名时，同步调用 `AmMetaJson::migrateFolderCache(oldFolderPath, newFolderPath)`，确保离散元数据连续不丢失。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/MetadataDefs.h`（增加 `ItemMeta` 的 `width`, `height`, `autoColor`, `addedAt` 属性字段）
- [ ] `src/meta/AmMetaJson.cpp`（重构 `itemToEntry` 与 `entryToItem`，对齐这 4 个新增属性的序列化与反序列化逻辑）
- [ ] `src/ui/ContentPanel.h`（增加 `DataSourceType` 强类型枚举及 `dataSourceType()` / `isMirrorSource()` / `isManagedContext()` 契约接口）
- [ ] `src/ui/ContentPanel.cpp`（解封 `setData` 编辑阻拦，重构 `onPathsDropped` 拖放分流中枢，`scanDir` 自动装载 `AmMetaJson`）
- [ ] `src/meta/MetadataManager.h` / `src/meta/MetadataManager.cpp`（实现 `saveToDiskModeJson`，重构全套元数据设定接口实现 SQLite 与 `AmMetaJson` 的 100% 隔离分流）

**明确禁止越界修改的范围：**
- [ ] 托管库内 `.arc` 资产包封装格式 —— 不修改
- [ ] SQLite 数据库底层读写驱动 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **禁止字符串硬编码判断**：代码中严禁出现 `if (m_currentCategoryType == "all")` 或 `if (path.contains("://"))` 这种散落的临时判断，必须统一调用 `dataSourceType()`、`isMirrorSource()` 或 `isManagedContext()` 契约接口。
2.  **绝对隔离，零数据倒灌**：磁盘模式下的打标数据只许进 `ArcMeta.cache/*.json`，绝不许在 SQLite 数据库里创建任何记录或增加 `s_totalFileCount` 计数。
3.  **非阻塞后台 I/O**：加载与保存 `AmMetaJson` 的过程须确保在非主线程（或异步微秒级落盘）中执行，保证磁盘导航时列表滚动绝对流畅。
4.  **头文件依赖保障**：在 `ContentPanel.cpp` 中安全包含 `"../meta/AmMetaJson.h"` 以防未声明类编译错误。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨元数据分流 | 托管库内写入 SQLite 数据库，磁盘导航模式任意外部物理文件夹 100% 独占写入 `ArcMeta.cache/*.json`，绝不上锁，零 SQLite 污染 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。规则与契约已完全界定清楚，方案正式冻结，随时可以开始执行！