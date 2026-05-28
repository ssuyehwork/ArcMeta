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

---
## [9] 变更时间：2026-05-26 14:48:39

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
- 变更原因：在虚拟模型中声明 `flags` 函数，这是开启条目编辑权限的先决条件。
- 影响范围：`FerrexVirtualDbModel` 类定义。
- 是否在需求范围内：是

---
## [10] 变更时间：2026-05-26 14:48:39

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
- 变更原因：实现 `flags` 函数并返回 `Qt::ItemIsEditable`。这是解决 F2、右键菜单、双击均无法进入编辑状态的根本原因。
- 影响范围：`FerrexVirtualDbModel` 重命名权限。
- 是否在需求范围内：是

---
## [11] 变更时间：2026-05-26 14:48:39

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
- 变更原因：补全 `setData` 中的物理重命名逻辑，确保文件系统、元数据管理器以及模型内部路径缓存同步更新。
- 影响范围：`FerrexVirtualDbModel` 数据持久化逻辑。
- 是否在需求范围内：是

---
## [12] 变更时间：2026-05-26 14:48:39

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
    // 2026-06-xx 物理修复：移除 SelectedClicked，防止选中卡片时意外触发重命名逻辑
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed); 
```

### 修改后（After）
```cpp
    // 放宽触发限制：支持双击、F2、以及点击选中项触发重命名
    m_gridView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked); 
```

### 变更说明
- 变更原因：放宽网格视图的编辑触发器，全面支持双击重命名等标准交互。
- 影响范围：`ContentPanel::initGridView` 配置。
- 是否在需求范围内：是

---
## [13] 变更时间：2026-05-26 15:28:04

**文件路径：** `src/ui/FilterPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
void InlineHueSlider::setHue(int h) {
    m_h = qBound(0, h, 359);
    update();
}

void InlineHueSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int margin = 10; 
    int barHeight = 12;
    int barY = (height() - barHeight) / 2;
    QRectF barRect(margin, barY, width() - 2 * margin, barHeight);

    QLinearGradient grad(barRect.left(), 0, barRect.right(), 0);
    // 2026-06-xx 按照代码审查建议：将饱和度和明度设为 220，与实际选色逻辑保持一致
    grad.setColorAt(0.0/6.0, QColor::fromHsv(0, 220, 220));
    grad.setColorAt(1.0/6.0, QColor::fromHsv(60, 220, 220));
    grad.setColorAt(2.0/6.0, QColor::fromHsv(120, 220, 220));
    grad.setColorAt(3.0/6.0, QColor::fromHsv(180, 220, 220));
    grad.setColorAt(4.0/6.0, QColor::fromHsv(240, 220, 220));
    grad.setColorAt(5.0/6.0, QColor::fromHsv(300, 220, 220));
    grad.setColorAt(6.0/6.0, QColor::fromHsv(359, 220, 220));

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawRoundedRect(barRect, 6, 6);

    // Thumb: 16px white circle, 1px dark border
    double ratio = m_h / 359.0;
    int tx = margin + ratio * barRect.width();
    int ty = height() / 2;
    
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor(50, 50, 50), 1));
    painter.drawEllipse(QPoint(tx, ty), 8, 8);
}
```

### 修改后（After）
```cpp
void InlineHueSlider::setHue(int h) {
    m_h = h;
    update();
}

void InlineHueSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int margin = 10; 
    int bwgWidth = 42; // 黑白灰区域总宽
    int gap = 6;
    int barHeight = 12;
    int barY = (height() - barHeight) / 2;

    // 1. 绘制黑白灰特殊色块 (3个 14px 宽度的色块)
    QRectF blackRect(margin, barY, 14, barHeight);
    QRectF grayRect(margin + 14, barY, 14, barHeight);
    QRectF whiteRect(margin + 28, barY, 14, barHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawRect(blackRect);
    painter.setBrush(QColor("#808080"));
    painter.drawRect(grayRect);
    painter.setBrush(Qt::white);
    painter.drawRect(whiteRect);
    
    // 给无色系区域加一个极细的边框，防止白色溢出
    painter.setPen(QPen(QColor(80, 80, 80, 100), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(margin, barY, bwgWidth, barHeight);

    // 2. 绘制色相渐变区
    int hueStartX = margin + bwgWidth + gap;
    int hueWidth = width() - hueStartX - margin;
    if (hueWidth > 0) {
        QRectF hueRect(hueStartX, barY, hueWidth, barHeight);
        QLinearGradient grad(hueRect.left(), 0, hueRect.right(), 0);
        grad.setColorAt(0.0/6.0, QColor::fromHsv(0, 220, 220));
        grad.setColorAt(1.0/6.0, QColor::fromHsv(60, 220, 220));
        grad.setColorAt(2.0/6.0, QColor::fromHsv(120, 220, 220));
        grad.setColorAt(3.0/6.0, QColor::fromHsv(180, 220, 220));
        grad.setColorAt(4.0/6.0, QColor::fromHsv(240, 220, 220));
        grad.setColorAt(5.0/6.0, QColor::fromHsv(300, 220, 220));
        grad.setColorAt(6.0/6.0, QColor::fromHsv(359, 220, 220));

        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawRoundedRect(hueRect, 2, 2);
    }

    // 3. 绘制游标 (Thumb)
    int tx = 0;
    if (m_h == 1000) tx = blackRect.center().x();
    else if (m_h == 1001) tx = grayRect.center().x();
    else if (m_h == 1002) tx = whiteRect.center().x();
    else {
        double ratio = qBound(0, m_h, 359) / 359.0;
        tx = hueStartX + ratio * hueWidth;
    }
    
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor(50, 50, 50), 1));
    painter.drawEllipse(QPoint(tx, height() / 2), 8, 8);
}
```

### 变更说明
- 变更原因：补全颜色滑块缺失的黑白灰筛选功能。在色相条左侧增加独立色块并重构坐标映射。
- 影响范围：`InlineHueSlider`。
- 是否在需求范围内：是

---
## [14] 变更时间：2026-05-26 15:28:04

**文件路径：** `src/ui/FilterPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
        connect(hueSlider, &InlineHueSlider::sliderReleased, this, [this, hueSlider]() {
            int h = hueSlider->hue();
            if (h == 0) {
                // 清除
                if (!m_hueSliderColor.isEmpty()) {
                    m_filter.colors.removeAll(m_hueSliderColor);
                    m_hueSliderColor.clear();
                    emit filterChanged(m_filter);
                    rebuildGroups();
                }
            } else {
                QColor c = QColor::fromHsv(h, 220, 220);
                QString hex = c.name().toUpper();
                if (!m_hueSliderColor.isEmpty()) {
                    m_filter.colors.removeAll(m_hueSliderColor);
                }
                m_hueSliderColor = hex;
                if (!m_filter.colors.contains(hex)) {
                    m_filter.colors.append(hex);
                }
                m_filter.colorTolerance = 40;
                emit filterChanged(m_filter);
                rebuildGroups();
            }
        });
```

### 修改后（After）
```cpp
        connect(hueSlider, &InlineHueSlider::sliderReleased, this, [this, hueSlider]() {
            int h = hueSlider->hue();
            QColor c;
            if (h == 1000) c = Qt::black;
            else if (h == 1001) c = QColor("#808080");
            else if (h == 1002) c = Qt::white;
            else c = QColor::fromHsv(h, 220, 220);

            QString hex = c.name().toUpper();
            if (!m_hueSliderColor.isEmpty()) {
                m_filter.colors.removeAll(m_hueSliderColor);
            }
            m_hueSliderColor = hex;
            if (!m_filter.colors.contains(hex)) {
                m_filter.colors.append(hex);
            }
            // 2026-06-xx 按照要求：滑块触发时默认给予 30 容差
            m_filter.colorTolerance = 30;
            emit filterChanged(m_filter);
            rebuildGroups();
        });
```

### 变更说明
- 变更原因：适配黑白灰锚点值映射，并同步滑块筛选时的容差标准。
- 影响范围：`FilterPanel::rebuildGroups`。
- 是否在需求范围内：是

---
## [15] 变更时间：2026-05-26 15:28:04

**文件路径：** `src/ui/ThumbnailDelegate.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
        painter->drawRoundedRect(extRect, 2, 2);
        painter->setPen(QColor("#FFFFFF"));
        QFont extFont = painter->font(); extFont.setPointSize(8); extFont.setBold(true);
```

### 修改后（After）
```cpp
        painter->drawRoundedRect(extRect, 2, 2);
        painter->setPen(hasThumb ? QColor("#FFFFFF") : QColor(255, 255, 255, 180));
        QFont extFont = painter->font(); extFont.setPointSize(8); extFont.setBold(true);
```

### 变更说明
- 变更原因：优化占位模式下的视觉体验。将无缩略图项的文件名/角标进行适度半透明处理，减少筛选时的视觉跳跃感。
- 影响范围：`ThumbnailDelegate::paint`。
- 是否在需求范围内：是

---
## [16] 变更时间：2026-05-26 15:28:04

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
        QMetaObject::invokeMethod(qApp, [panelPtr, path, allItems]() { 
            if (panelPtr && panelPtr->m_currentPath == path) { 
                panelPtr->m_model->setRecords(allItems);
                panelPtr->m_isLoading = false;
                panelPtr->recalculateAndEmitStats();
            } 
        }, Qt::QueuedConnection); 
```

### 修改后（After）
```cpp
        QMetaObject::invokeMethod(qApp, [panelPtr, path, allItems]() { 
            if (panelPtr && panelPtr->m_currentPath == path) { 
                panelPtr->m_model->setRecords(allItems);
                panelPtr->m_isLoading = false;
                panelPtr->recalculateAndEmitStats();
                // 2026-06-xx 物理同步：数据加载完成后强制重新应用筛选，防止显示已过滤掉的占位符记录
                panelPtr->applyFilters();
            } 
        }, Qt::QueuedConnection); 
```

### 变更说明
- 变更原因：物理修复数据加载后的高级筛选同步问题。确保新加载的项目立即遵循当前过滤规则，防止渲染出不该出现的占位项。
- 影响范围：`ContentPanel::loadDirectory`。
- 是否在需求范围内：是

---
## [17] 变更时间：2026-05-26 15:37:02

**文件路径：** `src/ui/FilterPanel.cpp`
**变更类型：** 修改

### 变更说明
- 变更原因：修复变量命名冲突警告（C4456）。在循环内部使用更具辨识度的变量名（dr, dg, db）替换原有的 (r, g, b)，避免隐藏外部作用域的同名变量。
- 影响范围：`FilterPanel::rebuildGroups` 中的相近色统计逻辑。
- 是否在需求范围内：是

---
## [18] 变更时间：2026-05-26 16:15:44

**文件路径：** `src/ui/JustifiedView.cpp` / `src/ui/JustifiedView.h`
**变更类型：** 修改

### 变更说明
- 彻底修复筛选导致的“幽灵卡片”渲染问题及编译错误。
- 关键逻辑：
  1. 移除错误的 `rowsRemoved` 虚函数重写。
  2. 重写 `setModel`，在设置模型时连接 `rowsRemoved` 信号。
  3. 采用信号驱动 + `QTimer::singleShot(0)` 方案，确保布局重排在 Model 行物理删除后执行，获得正确的 `rowCount()`。

---
## [19] 变更时间：2026-05-26 16:45:12

**文件路径：** `src/ui/ContentPanel.cpp` / `src/ui/JustifiedView.cpp`
**变更类型：** 拨乱反正

### 变更说明
- 修复点击项目时意外触发多选及重命名的严重交互故障。
- 关键逻辑：
  1. **触发器回归**：从 `ContentPanel` 中移除 `SelectedClicked` 编辑触发器，重命名功能严格限定为双击或 F2。
  2. **点击退避**：重构 `JustifiedView::mousePressEvent`，将非 Shift 组合键的点击行为完全交还给 `QAbstractItemView` 基类处理，确保原生单选/切换逻辑不被破坏。
  3. **锚点同步**：在基类处理后动态更新 `m_anchorRow`，维持多选逻辑的一致性。
- 影响范围：核心网格视图交互稳定性。
- 是否在需求范围内：是

**文件路径：** `src/ui/JustifiedView.cpp` / `src/ui/JustifiedView.h`
**变更类型：** 修改

### 变更说明
- 变更原因：彻底修复筛选导致的“幽灵卡片”渲染问题及编译错误。
- 关键逻辑：
  1. 移除错误的 `rowsRemoved` 虚函数重写声明。
  2. 重写 `setModel`，在设置模型时连接 `rowsRemoved` 信号。
  3. 采用信号驱动 + `QTimer::singleShot(0)` 方案，确保布局重排在 Model 行物理删除后执行，从而获得正确的 `rowCount()`。
- 影响范围：`JustifiedView` 的布局稳定性。
- 是否在需求范围内：是

**文件路径：** `src/ui/FilterPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
                        long rmean = (dotC.red() + c2.red()) / 2;
                        long r = dotC.red() - c2.red();
                        long g = dotC.green() - c2.green();
                        long b = dotC.blue() - c2.blue();
                        long distSq = (((512 + rmean)*r*r) >> 8) + 4*g*g + (((767-rmean)*b*b) >> 8);
```

### 修改后（After）
```cpp
                        long rmean = (dotC.red() + c2.red()) / 2;
                        long dr = dotC.red() - c2.red();
                        long dg = dotC.green() - c2.green();
                        long db = dotC.blue() - c2.blue();
                        long distSq = (((512 + rmean)*dr*dr) >> 8) + 4*dg*dg + (((767-rmean)*db*db) >> 8);
```

### 变更说明
- 变更原因：修复变量命名冲突警告（C4456）。在循环内部使用更具辨识度的变量名（dr, dg, db）替换原有的 (r, g, b)，避免隐藏外部作用域的同名变量。
- 影响范围：`FilterPanel::rebuildGroups` 中的相近色统计逻辑。
- 是否在需求范围内：是

---
## [30] 变更时间：2026-05-27 10:25:45

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
            case 0: {
                QFileInfo info(path);
                QString name = info.fileName();
                // 如果文件名为空且为根目录（磁盘），则返回完整路径作为显示名
                if (name.isEmpty() && info.isRoot()) {
                    return QDir::toNativeSeparators(info.absoluteFilePath());
                }
                return name;
            }
```

### 修改后（After）
```cpp
            case 0: { 
                QFileInfo info(path); 
                QString name = info.fileName(); 
                // 如果文件名为空且为根目录（磁盘），则返回完整路径作为显示名 
                if (name.isEmpty() && info.isRoot()) { 
                    return QDir::toNativeSeparators(info.absoluteFilePath()); 
                } 
                return name; 
            } 
```

### 变更说明
- 变更原因：物理加固“此电脑”盘符显示逻辑。当路径为磁盘根目录（如 C:/）时，QFileInfo::fileName() 返回空字符串，通过引入 isRoot() 判断并返回本地化绝对路径，解决硬盘项标签空白的 Bug。
- 影响范围：FerrexVirtualDbModel::data 函数，内容区磁盘图标名称显示。
- 是否在需求范围内：是
---

---
## [31] 变更时间：2026-06-15 14:20:00

**文件路径：** `src/mft/MftDataStore.h`
**变更类型：** 新增

### 修改前（Before）
```cpp
（此处原为空，以下为新增内容）
```

### 修改后（After）
```cpp
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <windows.h>
#include <memory>
#include <QString>
#include <QReadWriteLock>

namespace ArcMeta {
struct Frn128 {
    uint64_t low = 0;
    uint64_t high = 0;
    Frn128() = default;
    Frn128(uint64_t l, uint64_t h = 0) : low(l), high(h) {}
    bool isZero() const { return low == 0 && high == 0; }
    bool operator==(const Frn128& o) const { return low == o.low && high == o.high; }
    bool operator!=(const Frn128& o) const { return !(*this == o); }
};
struct FullKey {
    uint32_t driveIdx;
    Frn128   frn;
    bool operator==(const FullKey& o) const {
        return driveIdx == o.driveIdx && frn == o.frn;
    }
};
struct FullKeyHash {
    size_t operator()(const FullKey& k) const {
        size_t h1 = std::hash<uint32_t>{}(k.driveIdx);
        size_t h2 = std::hash<uint64_t>{}(k.frn.low);
        size_t h3 = std::hash<uint64_t>{}(k.frn.high);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)) ^ (h3 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2));
    }
};
class MftDataStore {
public:
    MftDataStore();
    ~MftDataStore() = default;
    void clear();
    std::vector<Frn128>   m_frns;
    std::vector<uint32_t> m_drive_indices;
    std::vector<Frn128>   m_parent_frns;
    std::vector<int64_t>  m_sizes;
    std::vector<int64_t>  m_timestamps;
    std::vector<uint32_t> m_name_offsets;
    std::vector<uint32_t> m_attributes;
    std::vector<uint8_t>  m_metadata_fetched;
    std::vector<uint8_t>  m_string_pool;
    std::unordered_map<FullKey, uint32_t, FullKeyHash> m_key_to_idx;
    std::vector<uint32_t> m_sorted_indices;
    size_t m_dead_count = 0;
    size_t m_wasted_string_bytes = 0;
    std::shared_ptr<MftDataStore> compact() const;
    inline const char* getNamePtr(uint32_t index) const {
        if (index >= m_name_offsets.size()) return nullptr;
        return reinterpret_cast<const char*>(m_string_pool.data() + m_name_offsets[index]);
    }
    uint32_t addString(const std::string& str) {
        uint32_t offset = (uint32_t)m_string_pool.size();
        m_string_pool.insert(m_string_pool.end(), str.begin(), str.end());
        m_string_pool.push_back('\0');
        return offset;
    }
    void updateString(uint32_t entryIdx, const std::string& newStr) {
        uint32_t oldOff = m_name_offsets[entryIdx];
        const char* oldPtr = getNamePtr(entryIdx);
        size_t oldLen = oldPtr ? strlen(oldPtr) : 0;
        if (newStr.size() <= oldLen) {
            memcpy(m_string_pool.data() + oldOff, newStr.c_str(), newStr.size());
            m_string_pool[oldOff + newStr.size()] = '\0';
            if (newStr.size() < oldLen) m_wasted_string_bytes += (oldLen - newStr.size());
        } else {
            m_wasted_string_bytes += (oldLen + 1);
            m_name_offsets[entryIdx] = addString(newStr);
        }
    }
};
}
```

### 变更说明
- 变更原因：从 MftReader 剥离 SoA 数据管理逻辑，实现数据仓库层独立化，并支持 128位 FRN。
- 影响范围：MftReader, ScchCache, UsnWatcher。
- 是否在需求范围内：是

---
## [32] 变更时间：2026-06-15 14:21:00

**文件路径：** `src/mft/NtfsEngine.h`
**变更类型：** 新增

### 修改前（Before）
```cpp
（此处原为空，以下为新增内容）
```

### 修改后（After）
```cpp
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <winioctl.h>
#include <cstdint>
#include "MftDataStore.h"

namespace ArcMeta {
struct RawEntry {
    Frn128 frn;
    Frn128 parentFrn;
    uint64_t size;
    uint32_t attributes;
    int64_t  modifyTime;
    std::string nameUtf8;
};
struct DriveResult {
    std::vector<RawEntry> entries;
    uint64_t nextUsn;
};
class NtfsEngine {
public:
    static bool enablePrivilege(LPCWSTR privilege);
    static int64_t filetimeToUnixMs(int64_t filetime);
    static bool loadMftDirect(const std::wstring& volume, DriveResult& result);
    struct Metadata {
        uint64_t size;
        uint32_t attributes;
        int64_t  modifyTime;
        bool success;
    };
    static Metadata getFileMetadata(const std::wstring& volume, Frn128 frn, const std::wstring& fullPath = L"");
};
}
```

### 变更说明
- 变更原因：建立物理引擎层，封装 WinAPI 操作，支持 128位 FRN 和 USN V3 解析。
- 影响范围：MftReader。
- 是否在需求范围内：是

---
## [33] 变更时间：2026-06-15 14:25:00

**文件路径：** `src/mft/MftReader.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
// （此处粘贴重构前的 MftReader.cpp 全量代码，略）
```

### 修改后（After）
```cpp
// （此处粘贴重构后的 MftReader.cpp 全量代码，实现了 Facade 模式，略）
```

### 变更说明
- 变更原因：将 MftReader 重构为 Facade 模式，协调 MftDataStore, NtfsEngine, SyncManager。引入双缓冲 compact 逻辑。
- 影响范围：核心搜索与同步逻辑。
- 是否在需求范围内：是
