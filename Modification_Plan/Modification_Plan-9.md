# 内容面板右键菜单选项双轨强隔离分流重构 —— Modification_Plan-9.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 DAM（数字资产管理）系统的一等公民分类重构中，主程序为托管库（`ArcMeta.Library_X`）与外部物理磁盘导航建立了清晰的双轨路由机制。然而，在内容面板（`ContentPanel`）的右键菜单中，共有 25 个右键菜单选项，部分写操作与物理交互选项没有完成彻底的物理和逻辑隔离。

本方案作为一个独立、纯净的新话题，旨在对右键菜单中涉及设定颜色标签、单项重命名、批量重命名、删除、归类与迁移、粘贴、刷新等 7 个核心写操作/物理动作选项，进行强类型契约 `DataSourceType` 判定分流，保障在托管库内 100% 写入数据库，在普通磁盘浏览模式下 100% 隔离写入 `AmMetaJson` 缓存，实现完美的双轨物理机制隔离和无缝操作体验。

---

## 2. 问题定位
*   **双轨写操作混杂**：
    在 `src/ui/ContentPanel.cpp` 的 `onCustomContextMenuRequested` 以及关联处理函数中，删除、粘贴、重命名、设定颜色标签等逻辑，存在不一致的写回操作。
    - **设定颜色标签**：使用 `m_proxyModel->setData(idx, hexColor, ColorRole)`，通过 Model 接口写回。在非托管路径下可能无法保障完全与 SQLite 物理剥离；
    - **单项重命名**：在 `view->edit(currentIndex)` 调起的 Delegate `setModelData` 内部，若非托管路径则应该触发物理文件重命名并迁移 `AmMetaJson` 记录和内存缓存，绝不写入 SQLite；
    - **批量重命名**：`performBatchRename` 在磁盘模式下应批量执行 `ShellHelper::renameItem`，并同步更新 `.Arcmeta.json` 里的 key，迁移缩略图及宽高比内存缓存防止闪烁变灰；
    - **删除**：在磁盘模式下误调用数据库逻辑会污染 DB。应当执行物理回收站删除或 `asyncDeletePaths` 并擦除离散缓存；
    - **粘贴**：托管库粘贴执行资产导入，磁盘模式执行物理复制并在对应路径局部重新加载。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 设定颜色标签 (ColorStripPicker)：托管库模式：写入 SQLite 数据库 `metadata` 表；磁盘模式：写入 `.Arcmeta.json` 离散文件（`AmMetaJson`），**绝对不写入 SQLite**。 | 详见 4.1 节。在 `ColorStripPicker` 的 `colorSelected` 回调中进行 `isManagedContext()` 精准判定：库内写入 SQLite（通过 model 处理）；库外物理写入 `.Arcmeta.json`。 | ✅ |
| 2    | 单项重命名 (`ActionRename` / 行内编辑)：托管库模式：**纯逻辑重命名**。仅改 SQLite 表里的 `name` 字段，物理 `.arc` 容器名与源文件名 100% 不变（0 开销、不上锁）；磁盘模式：**物理文件重命名**。调用 Windows `ShellHelper::renameItem` 对硬盘文件重命名，并同步更新 `.Arcmeta.json` 里的条目 Key。 | 详见 4.2 节。在 `setData` 的重命名逻辑和 Edit delegate 保存逻辑中判定，普通磁盘导航模式执行物理 `renameItem` 并更新离散 JSON 缓存。 | ✅ |
| 3    | 批量重命名 (`ActionBatchRename`)：托管库模式：批量修改 SQLite 表里的 `name` 字段；磁盘模式：批量执行真实物理重命名，并同步更新 `.Arcmeta.json`。 | 详见 4.3 节。重构 `performBatchRename` 的应用逻辑，磁盘模式下执行物理重命名，同步改写 `.Arcmeta.json`。 | ✅ |
| 4    | 磁盘模式重命名后执行无损缓存 Key 迁移 | 详见 4.3 节。重构磁盘重命名完毕后的主线程回调，将 `oldPath` 对应的 `m_iconCache` 与 `m_aspectRatios` 等缓存无损继承至 `newPath` 之下，防止变灰。 | ✅ |
| 5    | 删除 (`ActionDelete`/`ActionSecureDelete`)：托管库模式：从 SQLite 移除关联或将 `is_trash = 1`；磁盘模式：调用系统回收站 (`moveToTrash`) 或 `SecureFileEraser` 物理抹除，并删除 `.Arcmeta.json` 中的该条目。 | 详见 4.4 节。对删除和深层抹除进行 `isManagedContext()` 隔离判定。磁盘模式下物理删除并擦除对应的离散缓存。 | ✅ |
| 6    | 归类到 (`ActionCategorize`) vs 迁移 (`ActionAddToCategory`)：托管库模式：显示“归类到...”，修改 SQLite 的 `category_items` 绑定关系；磁盘模式：显示“迁移”，调用 `AssetImporter::importAssets` 执行 `.arc` 资产包封装入库。 | 详见 4.5 节。在右键菜单生成时进行 `isMirrorSource()` 语义分流。镜像源显示归类，普通磁盘源在有托管库时显示“迁移”。 | ✅ |
| 7    | 粘贴 (`ActionPaste` / 拖放)：托管库模式：调用 `AssetImporter` 执行打包与元数据入库；磁盘模式：执行标准的 Windows 操作系统级物理文件复制/粘贴到当前目录 `m_currentPath`。 | 详见 4.6 节。对粘贴动作进行双轨分流，磁盘模式下执行 Windows Shell 复制移动，并在粘贴成功后触发局部的缓存清理与同步。 | ✅ |
| 8    | 刷新 (`ActionRefresh` / `refreshAll`)：托管库/系统视图：调用 `loadCategory()` 或 `loadPaths()` 重新查库；磁盘模式：调用 `loadDirectory()` 重新扫描物理磁盘并加载 `.Arcmeta.json`。 | 详见 4.7 节。在载入和刷新时自动扫描 `.Arcmeta.json` 并对已物理不存在的孤立元数据项目进行静默清理，保障缓存干净度。 | ✅ |

---

## 4. 详细解决方案

### 4.1 解决：设定颜色标签 (ColorStripPicker) 路由分流
在 `src/ui/ContentPanel.cpp` 中的 `ColorStripPicker` `colorSelected` 回调中重构设色写入：
*   **库内分流**：如果当前正处于 `isManagedContext()` 内，执行原 `m_proxyModel->setData(idx, hexColor, ColorRole)` 逻辑，将其安全写入 SQLite 的 `metadata` 数据库。
*   **库外分流**：如果处于磁盘导航模式，由于对应的行项目无法写回 SQLite，我们在获取多选路径后，遍历执行 `AmMetaJson::saveColor(path, hexColor)` 并在主线程触发视图重绘 `m_proxyModel->invalidate()`，100% 隔离 SQLite 数据库写动作。

### 4.2 解决：单项重命名 (`ActionRename` / 行内编辑) 路由分流
当用户通过 F2 或菜单对单元格项执行重命名操作时：
*   **库内模式**：Delegate 执行 `setData`，并在 `FerrexVirtualDbModel::setData` / `MetadataManager::setDisplayName` 内部判定：如果是托管项，只执行 SQLite 数据的修改（纯逻辑重命名，物理文件名保持 `.arc` 容器及原名 100% 不动）；
*   **磁盘模式**：在 Model 的 `setData` 回调或编辑写入点中判断不属于 `isManagedContext()`，则同步调用 Windows 原生 `ShellHelper::renameItem` 对硬盘物理文件执行真实改名，同时调用 `AmMetaJson::renameEntry(oldPath, newPath)` 将缓存 JSON 键无损更名，接着进行无损缓存 Key 迁移（缩略图、宽高比等继承）。

### 4.3 解决：批量重命名 (`ActionBatchRename`) 路由分流与缓存自愈
在 `ContentPanel::performBatchRename()` 中：
*   **库内模式**：弹出 `BatchRenameDialog` 并对入库的数据批量更新 SQLite 对应的 `name`（纯逻辑修改）；
*   **磁盘模式**：
    1.  调用 `BatchRenameDialog`。对话框在确认重命名时在底层执行真实硬盘文件改名并同步改写对应的 `.Arcmeta.json` 项。
    2.  在对话框成功 Accept 并回到主线程后，遍历重命名清单，提取原保存在旧路径 `oldPath` 下的缓存，执行 **无损缓存 Key 迁移**：
        ```cpp
        // 伪代码示例：无损转移磁盘元数据缓存，彻底阻断闪烁
        auto oldIcon = m_iconCache.take(oldPath);
        if (oldIcon) m_iconCache.insert(newPath, oldIcon);
        if (m_aspectRatios.contains(oldPath)) {
            m_aspectRatios.insert(newPath, m_aspectRatios.take(oldPath));
        }
        ```

### 4.4 解决：删除 (ActionDelete / ActionSecureDelete) 路由分流
在 `ContentPanel.cpp` 中的删除响应 switch 分支：
*   **库内模式**：如果是托管项目，删除仅从 SQLite 中解除与该分类的绑定或将 `is_trash = 1`，不可伤及真实的物理源文件；
*   **磁盘模式**：
    1.  如果是移入回收站（`ActionDelete`），执行 Windows Shell 原生删除 `ShellHelper::moveToTrash(targetPaths)`；
    2.  如果是永久删除（`ActionSecureDelete`），执行 `DiskIoService::asyncDeletePaths` 安全覆盖抹除并强力粉碎物理文件。
    3.  操作成功后，调用 `AmMetaJson::removeEntries(targetPaths)` 将其彻底从该物理磁盘路径的 `.Arcmeta.json` 序列化结构中剥离抹除。

### 4.5 解决：归类 (ActionCategorize) 与迁移 (ActionAddToCategory) 的菜单及动作分流
*   **菜单显示分流**：
    - 在 `isMirrorSource()` （代表分类视图或已入库镜像状态）时，在右键菜单中仅展现 `归类到...` 子菜单与快捷色块栏，点击动作触发 SQLite 中的 `category_items` 分类绑定；
    - 在非 `isMirrorSource()` 的普通物理磁盘模式下，隐藏归类，显示 `迁移` 菜单。点击菜单调用 `AssetImporter::importAssets` 执行物理移动、打包封装及注册到当前的盘符托管库中。

### 4.6 解决：粘贴 (ActionPaste) 物理与逻辑双轨分流
在 `ContentPanel::performPaste()` 粘贴动作中：
*   **库内模式**：如果处于托管库生命周期内，调用 `ImportHelper::importPaths` 进行特征解析和入库打包；
*   **磁盘模式**：直接调用 `ShellHelper::copyOrMoveItems` 在指定物理路径间执行物理移动/复制，并在执行完毕后执行当前物理目录的重新加载。

### 4.7 解决：刷新 (ActionRefresh) 局部重新对账与孤立条目清理
在磁盘模式的 `loadDirectory(m_currentPath)` 以及刷新方法中：
*   重扫物理磁盘前，自动载入 `.Arcmeta.json`。
*   在装载物理项时，执行 **孤立垃圾数据清理**：核对 JSON 缓存中记录的每个文件路径是否依然在物理磁盘中真实存在（`QFile::exists(path)`）。如果某个路径已在外部被删除，调用 `AmMetaJson::removeEntry(path)` 进行内存与磁盘序列化文件的静默自动清洗，防止垃圾条目无限累积。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp`（重构 `onCustomContextMenuRequested`、`performPaste`、`performBatchRename` 和删除 case 分流，精细嵌入双轨 `DataSourceType` 的逻辑分流和缓存自愈、磁盘 JSON 清洗机制）
- [ ] `src/meta/AmMetaJson.cpp`（补齐单项设色、文件更名、条目物理抹除及孤立项目自动物理清洗工具接口，确保与 ContentPanel 操作 1:1 无损协同）

**明确禁止越界修改的范围：**
- [ ] 视图按钮与卡片网格绘制组件 `CardPainterHelper` —— 不修改
- [ ] 侧边栏折叠持久化机制 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **物理路径对齐**：在磁盘模式进行重命名和删除后，由于涉及原生 I/O 修改，新旧路径的路径符（正反斜杠）在 Windows 环境下必须一致转换（使用 `QDir::toNativeSeparators`），以避免在 `AmMetaJson` 中因为路径字符串不匹配而导致擦除或缓存更名失败。
2.  **避免数据库写倒灌**：在磁盘导航模式下执行重命名、删除或设色时，必须在代码中加上安全隔离判定保护，绝对禁止触发 `MetadataManager` 内涉及 SQLite 物理写入的相关成员接口。
3.  **大事务异步防御**：磁盘模式下的物理复制/删除，或者包含大量大文件的重命名，操作必须在工作线程（通过 `DiskIoService` 或 `QThreadPool`）中异步完成，操作成功后，使用安全弱指针 `QPointer<ContentPanel>` 回调并在主线程重绘刷新视图，以预防主界面由于长时间 I/O 锁盘产生卡死。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨元数据隔离 | 在库外普通磁盘模式下，元数据自动调用 AmMetaJson 写入 ArcMeta.cache/ 离散缓存中，确保不污染 SQLite 本地数据库与原始物理盘。 | ✅ 符合 |
| 无损缓存自愈 | 重命名物理路径变更成功后，缩略图及宽高比等高级元数据缓存执行无损 key 迁移继承，保证无缝的高性能浏览体验。 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。右键菜单 25 个选项的分布及 7 个核心选项的双轨精准分流逻辑已被完整隔离定位并冻结。