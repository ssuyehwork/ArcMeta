# Modification Record

---
## [1] 变更时间：2026-06-18 14:15:22

**文件路径：** `src/ui/ContentPanel.h` / `src/ui/ThumbnailDelegate.h`
**变更类型：** 修改

### 修改说明
- 物理清理了冗余的悬停状态追踪变量 `m_hoverIndex` 和 `m_hoverStar`，确保类定义纯净。

---
## [2] 变更时间：2026-06-18 14:20:45

**文件路径：** `src/ui/ContentPanel.cpp` / `src/ui/ThumbnailDelegate.cpp`
**变更类型：** 还原

### 修改前（Before）
```cpp
    // 包含悬停高亮逻辑的 paint
    if (isHoveringThis && m_hoverStar >= level) fill = true;
    // 处理 MouseMove 的 editorEvent
```

### 修改后（After）
```cpp
    // 彻底还原：
    // 1. paint 中仅根据 index.data(RatingRole) 绘制实心/空心星。
    // 2. editorEvent 仅处理 MouseButtonPress 点击事件。
    // 3. 移除所有 MouseMove 和 Leave 事件监听。
```

### 变更说明
- 变更原因：按照用户要求彻底还原星级部分，废除所有悬停高亮逻辑。恢复最原始、最稳健的评级交互表现。
- 影响范围：所有 UI Delegate。
- 是否在需求范围内：是

---
## [3] 变更时间：2026-06-18 14:25:00

**文件路径：** `Memories.md`
**变更类型：** 文档更新

### 修改说明
- 物理删除了此前记录的所有关于“悬停高亮”和“动态收缩”的规则，确保《记忆碎片》不被错误逻辑污染。


---
## [4] 变更时间：2026-05-26 14:02:23

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: return QFileInfo(path).fileName();
            case 4: {
```

### 修改后（After）
```cpp
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: {
                QFileInfo info(path);
                QString name = info.fileName();
                // 如果文件名为空且为根目录（磁盘），则返回完整路径作为显示名
                if (name.isEmpty() && info.isRoot()) {
                    return QDir::toNativeSeparators(info.absoluteFilePath());
                }
                return name;
            }
            case 4: {
```

### 变更说明
- 变更原因：修复“此电脑”界面下硬盘盘符显示为空的问题。当路径为根目录（如 C:/）时，QFileInfo::fileName() 返回空，需特殊处理返回完整路径并转换为本地分隔符。
- 影响范围：FerrexVirtualDbModel::data 函数，涉及网格视图与列表视图的名称列显示。
- 是否在需求范围内：是

---
## [5] 变更时间：2026-05-26 14:19:35

**文件路径：** `src/ui/ContentPanel.h`
**变更类型：** 修改

### 修改前（Before）
```cpp
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
```

### 修改后（After）
```cpp
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
```

### 变更说明
- 变更原因：为虚拟模型 `FerrexVirtualDbModel` 添加 `flags` 函数声明，以便支持条目编辑（重命名）。
- 影响范围：`FerrexVirtualDbModel` 类定义。
- 是否在需求范围内：是

---
## [6] 变更时间：2026-05-26 14:19:35

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
int FerrexVirtualDbModel::columnCount(const QModelIndex&) const {
    return 8; // 名称, 状态, 星级, 颜色标记, 标签, 类型, 大小, 修改日期
}

QVariant FerrexVirtualDbModel::data(const QModelIndex& index, int role) const {
```

### 修改后（After）
```cpp
int FerrexVirtualDbModel::columnCount(const QModelIndex&) const {
    return 8; // 名称, 状态, 星级, 颜色标记, 标签, 类型, 大小, 修改日期
}

Qt::ItemFlags FerrexVirtualDbModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    // 仅允许第 0 列（名称列）且非“分类”项进行重命名
    if (index.column() == 0) {
        if (index.row() < (int)m_allRecords.size() && !m_allRecords[index.row()].isCategory) {
            f |= Qt::ItemIsEditable;
        }
    }
    return f;
}

QVariant FerrexVirtualDbModel::data(const QModelIndex& index, int role) const {
```

### 变更说明
- 变更原因：实现 `flags` 函数，为名称列开启 `Qt::ItemIsEditable` 权限，使视图能够触发重命名。
- 影响范围：`FerrexVirtualDbModel` 成员函数。
- 是否在需求范围内：是

---
## [7] 变更时间：2026-05-26 14:19:35

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
bool FerrexVirtualDbModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) return false;

    // 虚拟模型中 setData 主要用于触发 UI 刷新，实际持久化由 MetadataManager 处理
    // 或是用于 QSortFilterProxyModel 的 mapToSource 联动
    emit dataChanged(index, index, {role});
    return true;
}
```

### 修改后（After）
```cpp
bool FerrexVirtualDbModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) return false;

    if (role == Qt::EditRole && index.column() == 0) {
        QString newName = value.toString();
        if (newName.isEmpty()) return false;

        auto& record = m_allRecords[index.row()];
        QString oldPath = record.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;

        if (oldPath != newPath && QFile::rename(oldPath, newPath)) {
            // 同步更新元数据索引
            MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
            // 物理同步：手动修改 m_allRecords 里的 path 以保持模型数据一致
            record.path = QDir::toNativeSeparators(newPath);
            emit dataChanged(index, index, {role, Qt::DisplayRole});
            return true;
        }
    }

    emit dataChanged(index, index, {role});
    return true;
}
```

### 变更说明
- 变更原因：补全 `setData` 中的物理重命名逻辑，确保文件系统操作与元数据同步执行。
- 影响范围：`FerrexVirtualDbModel::setData`。
- 是否在需求范围内：是

---
## [8] 变更时间：2026-05-26 14:19:35

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
    // 2026-06-xx 物理修复：移除 SelectedClicked，防止选中卡片时意外触发重命名逻辑
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed); 
```

### 修改后（After）
```cpp
    // 允许快捷键重命名以及点击已选中项重命名（Windows 标准行为）
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked); 
```

### 变更说明
- 变更原因：放宽重命名触发限制，允许通过点击已选中项触发重命名，符合 Windows 标准交互习惯。
- 影响范围：`ContentPanel::initGridView`。
- 是否在需求范围内：是
