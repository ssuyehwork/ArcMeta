## 分析计划 #26 ｜ [2026-05-26 14:15:45]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 继续严格按照下面提示去修复
>
> # 代码分析与修改方案 - 修复“此电脑”盘符显示与重命名失效问题
>
> ## 1. 问题背景
> 当前版本存在两个关键 UI 故障：
> 1. **盘符不显示**：点击“此电脑”后，硬盘图标下方为空白，无法识别 C 盘、D 盘。
> 2. **重命名无效**：对文件或文件夹进行重命名操作（双击、延迟点击或快捷键）均无反应，或无法进入编辑状态。
>
> ## 2. 故障深度分析
>
> ### 2.1 盘符显示失效原因
> 在 `src/ui/ContentPanel.cpp` 的 `FerrexVirtualDbModel::data()` 函数中，`Qt::DisplayRole` 的逻辑为：
> ```cpp
> case 0: return QFileInfo(path).fileName();
> ```
> 当路径为磁盘根目录（如 `C:/`）时，`fileName()` 返回值为空。由于新版本切换到了虚拟模型，未对这种特殊路径做降级处理。
>
> ### 2.2 重命名失效原因
> 重命名功能被两层“枷锁”封死：
> 1. **模型层缺失权限**：`FerrexVirtualDbModel` 没有重写 `flags()` 函数。Qt 模型默认不开启 `Qt::ItemIsEditable`，导致视图拒绝为该条目开启编辑器。
> 2. **视图层触发器受限**：`ContentPanel::initGridView` 中显式设置了 `m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed)`，这禁用了鼠标交互触发（如点击选中项），仅保留了 F2 键触发。
>
> ## 3. 详细修复方案
>
> ### 修改目标文件：`src/ui/ContentPanel.h`
>
> **修改点 A：补全模型编辑标志声明**
> 在 `FerrexVirtualDbModel` 类中增加 `flags` 函数声明：
> ```cpp
> // ...
> int columnCount(const QModelIndex& parent = QModelIndex()) const override;
> Qt::ItemFlags flags(const QModelIndex& index) const override; // 新增
> QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
> // ...
> ```
>
> ### 修改目标文件：`src/ui/ContentPanel.cpp`
>
> **修改点 B：实现 flags 函数以允许编辑**
> 在 `FerrexVirtualDbModel` 成员函数实现区添加：
> ```cpp
> Qt::ItemFlags FerrexVirtualDbModel::flags(const QModelIndex& index) const {
>     if (!index.isValid()) return Qt::NoItemFlags;
>     Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
>     // 仅允许第 0 列（名称列）且非“分类”项进行重命名
>     if (index.column() == 0) {
>         if (index.row() < (int)m_allRecords.size() && !m_allRecords[index.row()].isCategory) {
>             f |= Qt::ItemIsEditable;
>         }
>     }
>     return f;
> }
> ```
>
> **修改点 C：修复盘符显示逻辑 (Qt::DisplayRole)**
> 在 `FerrexVirtualDbModel::data` 函数中修改 `case 0`：
> ```cpp
> case 0: {
>     QFileInfo info(path);
>     QString name = info.fileName();
>     // 物理修复：如果文件名为空且是根目录，返回盘符
>     if (name.isEmpty() && info.isRoot()) {
>         return QDir::toNativeSeparators(info.absoluteFilePath());
>     }
>     return name;
> }
> ```
>
> **修改点 D：放宽重命名触发限制**
> 在 `ContentPanel::initGridView` 函数中（约 904 行），修改触发器设置：
> ```cpp
> // 允许快捷键重命名以及点击已选中项重命名（Windows 标准行为）
> m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
> ```
>
> **修改点 E：补全 setData 物理重命名执行逻辑**
> 在 `FerrexVirtualDbModel::setData` 中增加实际的文件系统操作：
> ```cpp
> bool FerrexVirtualDbModel::setData(const QModelIndex& index, const QVariant& value, int role) {
>     if (!index.isValid()) return false;
>
>     if (role == Qt::EditRole && index.column() == 0) {
>         QString newName = value.toString();
>         if (newName.isEmpty()) return false;
>
>         const auto& record = m_allRecords[index.row()];
>         QString oldPath = record.path;
>         QFileInfo info(oldPath);
>         QString newPath = info.absolutePath() + "/" + newName;
>
>         if (oldPath != newPath && QFile::rename(oldPath, newPath)) {
>             // 同步更新元数据索引
>             MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
>             // 注意：虚拟模型通常需要重新 loadDirectory 以更新 m_allRecords 里的 path，
>             // 或者此处手动修改 m_allRecords[index.row()].path = newPath;
>             return true;
>         }
>     }
>
>     emit dataChanged(index, index, {role});
>     return true;
> }
> ```
>
> ## 4. 验证测试
> 1. **盘符识别**：进入“此电脑”，硬盘下方必须显示 `C:\` 等路径。
> 2. **重命名触发**：
>    - 选中一个文件，再次点击文件名，应进入编辑框。
>    - 按下 F2 键，应进入编辑框。
> 3. **物理同步**：修改名称并回车后，资源管理器中的文件名称应同步变更。
>
> ## 5. 结论
> 通过同时修复模型层的 `flags` 标志、`data` 展示逻辑、`setData` 处理逻辑以及视图层的触发器设置，可以全面恢复因 AI 误改导致的 UI 交互功能缺失。

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.h | src/ui/ContentPanel.h | 内容面板头文件，声明虚拟模型 | 约 300 行 |
| ContentPanel.cpp | src/ui/ContentPanel.cpp | 内容面板实现，包含虚拟模型逻辑 | 约 1100 行 |

**1.2 现有架构问题定位**
- 问题一：盘符不显示（已在分析计划 25 中初步解决，但本计划要求更彻底的修复）
  - 根因：`QFileInfo::fileName()` 对根目录返回空。
- 问题二：重命名失效
  - 根因 1：`FerrexVirtualDbModel` 缺少 `flags()` 函数，未返回 `Qt::ItemIsEditable`。
  - 根因 2：`ContentPanel` 中的视图触发器 `EditTriggers` 过于严格。
  - 根因 3：`FerrexVirtualDbModel::setData` 仅发送信号，未执行物理重命名操作。

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
按照用户提供的详细方案执行。

**2.2 目标架构设计**
- 在 `FerrexVirtualDbModel` 中重写 `flags()`。
- 增强 `data()` 中的 `Qt::DisplayRole` 处理。
- 增强 `setData()` 中的物理重命名逻辑。
- 修改 `ContentPanel` 初始化中的视图触发器。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | ContentPanel.h | src/ui/ContentPanel.h | 修改 | 声明 `FerrexVirtualDbModel::flags` | 1 行 |
| 2 | ContentPanel.cpp | src/ui/ContentPanel.cpp | 修改 | 实现 `flags`，修改 `data`，修改 `setData`，修改 `initGridView` | ~30 行 |

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|--------|----------|
| 物理重命名失败 | 文件被占用或权限不足 | 3 | 9 | 在 `setData` 中检查 `QFile::rename` 返回值 |

### § 5 测试策略（Test Strategy）
- 验证“此电脑”盘符显示。
- 验证文件重命名交互（F2 与点击）。
- 验证重命名后的物理文件同步。

### § 7 执行前置条件
- [x] 用户已审阅并批准本分析计划

### § 8 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
