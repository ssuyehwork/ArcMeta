# 批量重命名支持数据库分类与磁盘双轨制无缝同步方案 —— Modification_Plan-105.md

> 状态：已批准，执行中

## 1. 任务背景
在目前版本中，批量重命名（`BatchRename`）功能在交互层面上存在着对物理磁盘导航视图模式的单一性绑定。

当用户在左侧侧边栏切换至数据库分类（User Category）、系统分类（如“全部数据”、“最近访问”、“回收站”等映射集合）时，右键菜单中虽然有时能够多选调起批量重命名，但在实际执行时，由于没有打通“视图源提取路径 -> 磁盘物理更名 -> 数据库元数据及关联 1:1 分类树同步修改”的闭环路径，这使得批量重命名功能无法在逻辑分类状态下正常应用，违背了数据库和物理磁盘一对一映射同步的设计宗旨。

本方案旨在重构并拓宽批量重命名的输入解析源和执行后的数据库刷新联动链路，从而实现全视图口径下的无缝批量重命名体验。

## 2. 问题定位
当前功能受阻的深层技术原因如下：
1. **多选路径提取对数据源的耦合**：在 `ContentPanel::performBatchRename()` 中，代码确实只是单纯通过 `view->selectionModel()->selectedIndexes()` 获取选中项。虽然选中项含有 `PathRole`（物理绝对路径），但在旧版本重构中，右键菜单的“批量重命名”在非物理 `nav` 视图模式下常被禁用，或没有将非物理模式路径（如分类模式下获取的 `PathRole`）视为合法的重命名输入，造成数据源输入被非法裁剪。
2. **重命名后分类数据库（categories表）同步脱节**：重命名成功后，目前仅执行了 `MetadataManager::instance().renameItem`，该函数只负责迁移 `metadata` 表中文件/文件夹对应的元数据（如星级、标签、颜色）。但是，**在 `categories` 表及 `category_items` 表中存储的分类物理绑定路径和物理路径提示（pathHint）却没有被同步重命名**！这会导致：
   - 数据库分类树中该物理文件夹对应的 1:1 镜像分类树节点因为路径对不齐而失效或变回空定义。
   - 分类包含关系发生断裂，导致重启后分类项无法恢复。
3. **缺少刷新后保持选中高亮状态的无缝自愈机制**：在执行完批量重命名后，原有的选中状态会被 `loadDirectory` / `loadCategory` 清屏抹去。用户在重命名后，原有的高亮虚化，破坏了连续重命名或进一步属性编辑的交互感。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 支持数据库里的文件夹或文件 | 只要选中项含有 `PathRole` 真实物理路径，无论当前处于 `nav` 磁盘模式还是 `user_category` / `system` 数据库分类模式，均完美允许批量重命名。 | ✅ |
| 2    | 数据库和磁盘存在映射同步关系 | 重命名成功后，不仅触发磁盘更名，还要同步调用 `MetadataManager` 进行元数据迁移，并修正 `categories` 表对应的 physicalPath。 | ✅ |
| 3    | 操作后仍然处于选中高亮状态 | 在更名成功、UI 重绘和数据库重新加载完毕后，使用暂存机制（`m_pendingSelectName`）重新自动定位并高亮选中最新的文件名。 | ✅ |

## 4. 详细解决方案

### 4.1 全口径无阻碍路径解析与提取（解决缺陷 1）
- **拓宽 `performBatchRename` 的输入来源**：
  在 `ContentPanel.cpp` 的 `performBatchRename()` 中，取消对当前视图模式（`m_currentCategoryType`）的任何前置限制。
- **获取物理路径的绝对真实路径**：
  ```cpp
  QModelIndexList indexes = getSelectedIndexes();
  std::vector<std::wstring> originalPaths;
  for (const auto& idx : indexes) {
      if (idx.column() == 0) {
          QString path = idx.data(PathRole).toString();
          if (!path.isEmpty()) {
              originalPaths.push_back(QDir::toNativeSeparators(path).toStdWString());
          }
      }
  }
  ```
  通过标准的 `PathRole`，即使该项位于数据库虚拟分类或最近访问中，其底层的物理路径依旧能被 100% 精准、无偏差提取。

### 4.2 磁盘物理更名与多维数据库全量对账（解决缺陷 2）
- **同步更新元数据表与分类关联表**：
  In `BatchRenameDialog::onExecute` 中，当 `QFile::rename(oldPath, newPath)` 执行成功后：
  1. 调用 `MetadataManager::instance().renameItem(oldWPath, newWPath)`，完成 `metadata` 表及内存 `m_cache` 中星级、标签、备注的原子化键值重映射迁移。
  2. 联动更新分类物理树：由于用户很可能对已入库并绑定了侧边栏分类的**物理文件夹**执行了更名，我们在 `CategoryRepo` 中新增一个核心物理重定义接口 `CategoryRepo::renamePhysicalCategoryPath(oldWPath, newWPath)`，将 `categories` 表中 `physicalPath` 与之匹配的项同步变更为 `newWPath`，并修改 `category_items` 表内可能存在的旧 `pathHint` 指针，彻底保住 1:1 分类树映射结构。

### 4.3 暂存自动对齐与选中自愈高亮（解决缺陷 3）
- **无痕选中恢复机制**：
  由于批量重命名完成后需要调用 `refreshAll()` 重新向模型载入最新数据，我们在刷新前，安全地将第一项被重命名后的新名称（或全量列表）暂存到 `m_pendingSelectName` 状态中：
  ```cpp
  if (!newNames.empty()) {
      // 暂存首项的新名称（含后缀），用于刷新后的自动选中
      m_pendingSelectName = QString::fromStdWString(newNames.front());
      m_isPendingEdit = false; // 不需要进入 F2 行内编辑，仅需保持选中高亮态
  }
  ```
- **视图定位自愈**：
  在 `ContentPanel::refreshAll()` 内，当异步加载（`loadDirectory` 或 `loadCategory`）执行完成并渲染出物理节点后，利用 `selectAndScrollToPath` 方法，自动对齐到最新的 `newPath` 并赋予 `QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows` 属性，使卡片或列表行重新完美高亮，保证流畅无断裂的无缝编辑感。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/BatchRenameDialog.cpp` （实现 `onExecute` 对 `QFile::rename` 的成功态检测，引入分类树路径迁移与元数据迁移双向同步，并暂存最新更名名）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` （在 `performBatchRename` 中彻底放开数据源路径解析，并在 `refreshAll()` 重绘回调后重新捕捉并赋予选中高亮）
- [ ] 模块/文件：`src/meta/CategoryRepo.h` & `src/meta/CategoryRepo.cpp` （追加 `renamePhysicalCategoryPath` 接口，以便将重命名关联的物理路径一并写入数据库 `categories` 的 `physicalPath` 和 `category_items` 的 `pathHint` 中）

**明确禁止越界修改的范围：**
- [ ] 规则行（`RuleRow`）界面排列布局——不修改
- [ ] `BatchRenameEngine` 规则链预览计算内核——不修改

## 6. 实现准则与预警【核心】
1. **防抖与抑制保护**：由于批量重命名会在极短时间内造成大量物理文件更名，这会诱发 Windows IOCP 向 `NativeFolderWatcher` 高频发送重命名或增删信号。在执行过程中，必须在 `BatchRenameDialog` 点火前后，安全利用 `MetadataManager::instance().setInternalOperating(true)` 开启锁，防止变动风暴和重构回调发生恶性竞态。
2. **事务原子性**：在 `BatchRenameDialog::onExecute` 中，对所有项的重命名应该通过数据库单事务提交。对于每一项物理重命名，必须在 `ok == true` 时才执行数据库字段迁移，如果某个物理文件因为占用等原因导致更名失败，保留其原有路径 and 元数据，不执行越权更新。
3. **开箱即用**：该重构必须完美兼容 `ListResultView` 与 `GridResultView` 以及 `JustifiedResultView` 这三种视图。当视图刷新完成后，不论用户当前处于列表斑马纹、自适应卡片还是网格拼图状态，该选中高亮机制都需完美工作。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 路径标准化  | 路径拼合和处理一律使用标准化规范，避免在后续对账或路径比对时发生大小写或斜杠不一致问题。 | ✅ 方案中使用 `QDir::toNativeSeparators` 和 `normalizePath` 进行转换，完全统一为原生标准化 wstring，避免因路径格式对不齐而无法命中 `categories` 或 `metadata`。 |
| UI 信号通知机制 | 跨线程执行或底层重构后，需优雅、高效地刷新 UI。 | ✅ 方案中不仅调用 `MetadataManager::instance().notifyUI` 发送局部和分类更新通知，而且在 UI 层使用 `QPointer` 弱指针保护，防止在大批量 I/O 耗时期间主窗体被销毁而引发指针异常访问，保证 100% 安全稳定。 |

## 8. 待确认事项（可选）
- 暂无
