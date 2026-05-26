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
