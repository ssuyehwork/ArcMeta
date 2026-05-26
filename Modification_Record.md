# Modification Record

---
## [1] 变更时间：2026-05-11 16:45:10

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
        // 物理锁定：评级辅助图标使用中性灰色，严禁脑补红色
        QColor baseColor = QColor("#CCCCCC");
        QColor starColor = colorName.isEmpty() ? baseColor : UiHelper::parseColorName(colorName).darker(700);
        QColor emptyStarColor = QColor("#888888");

        // 物理修复：移除禁止符的任何高亮逻辑，强制对齐中性灰色
```

### 修改后（After）
```cpp
        // 物理锁定：评级辅助图标使用中性灰色，严禁脑补红色
        QColor baseColor = QColor("#CCCCCC");
        QColor starColor = colorName.isEmpty() ? baseColor : UiHelper::parseColorName(colorName).darker(700);
        QColor emptyStarColor = QColor("#CCCCCC");

        // 物理修复：移除禁止符的任何高亮逻辑，强制对齐中性灰色
```

### 变更说明
- 变更原因：修复评级图标颜色显示，将空星颜色从深灰色改为中性灰色（#CCCCCC）以符合视觉标准。
- 影响范围：`GridItemDelegate::paint` 函数。
- 是否在需求范围内：是

---
## [2] 变更时间：2026-05-11 16:47:22

**文件路径：** `src/ui/ThumbnailDelegate.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
        bool isHoveringThis = (m_hoverIndex == index);
        bool shouldShowRating = (rating > 0) || isSelected || isHoveringThis;
        if (shouldShowRating) {
            // 物理锁定：评级区辅助图标（禁止符、空心星）始终使用低调的中性灰色，严禁脑补红色高亮
            QColor baseColor = QColor("#CCCCCC");
            QColor starColor = colorStr.isEmpty() ? baseColor : UiHelper::parseColorName(colorStr).darker(700);
            QColor emptyStarColor = QColor("#888888");

            // 物理修复：移除禁止符的红色高亮，使用修正后的 m.banRect 访问 Metrics 成员
            UiHelper::getIcon("no_color", baseColor, m.banRect.width()).paint(painter, m.banRect);

            QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(m.starSize, m.starSize), starColor);
            QPixmap emptyStar = UiHelper::getPixmap("star", QSize(m.starSize, m.starSize), emptyStarColor);

            // 工业级高亮：如果正在悬停，则悬停位置之前的星级均显示为“填充”状态
            for (int i = 0; i < 5; ++i) {
                int level = i + 1;
                bool fill = (level <= rating);
                if (isHoveringThis && m_hoverStar >= level) fill = true;

                painter->drawPixmap(m.starRect(i), fill ? filledStar : emptyStar);
            }
        }
```

### 修改后（After）
```cpp
        bool isHoveringThis = (m_hoverIndex == index);
        bool shouldShowRating = (rating > 0) || isSelected;
        if (shouldShowRating) {
            // 物理锁定：评级区辅助图标（禁止符、空心星）始终使用低调的中性灰色，严禁脑补红色高亮
            QColor baseColor = QColor("#CCCCCC");
            QColor starColor = colorStr.isEmpty() ? baseColor : UiHelper::parseColorName(colorStr).darker(700);
            QColor emptyStarColor = QColor("#CCCCCC");

            // 物理修复：移除禁止符的红色高亮，使用修正后的 m.banRect 访问 Metrics 成员
            UiHelper::getIcon("no_color", baseColor, m.banRect.width()).paint(painter, m.banRect);

            QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(m.starSize, m.starSize), starColor);
            QPixmap emptyStar = UiHelper::getPixmap("star", QSize(m.starSize, m.starSize), emptyStarColor);

            // 工业级高亮：如果正在悬停且已经处于显示状态，则显示“预测填充”状态
            for (int i = 0; i < 5; ++i) {
                int level = i + 1;
                bool fill = (level <= rating);
                if (isHoveringThis && m_hoverStar >= level) fill = true;

                painter->drawPixmap(m.starRect(i), fill ? filledStar : emptyStar);
            }
        }
```

### 变更说明
- 变更原因：修复评级区交互逻辑，取消未选中时的悬停显示，并修正空星颜色。
- 影响范围：`ThumbnailDelegate::paint` 函数。
- 是否在需求范围内：是

---
## [3] 变更时间：2026-05-11 16:55:05

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
void ContentPanel::search(const QString& query) {
    qDebug() << "[Content] 物理检索 (DB模式) ->" << query;
    if (m_viewStack) m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();

    m_isLoading = true;
    auto records = ItemRepo::searchRecordsByKeyword(query, m_currentPath);
    m_model->setRecords(records);
    m_isLoading = false;
    recalculateAndEmitStats();
}
```

### 修改后（After）
```cpp
void ContentPanel::search(const QString& query) {
    qDebug() << "[Content] 物理检索 (DB模式) ->" << query;
    if (m_viewStack) m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();

    m_isLoading = true;
    QString currentPath = m_currentPath;

    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, query, currentPath]() {
        auto records = ItemRepo::searchRecordsByKeyword(query, currentPath);

        QMetaObject::invokeMethod(qApp, [weakThis, records]() {
            if (weakThis) {
                weakThis->m_model->setRecords(records);
                weakThis->m_isLoading = false;
                weakThis->recalculateAndEmitStats();
            }
        });
    });
}
```

### 变更说明
- 变更原因：将关键词搜索逻辑异步化，防止大量结果查询时主线程卡死。
- 影响范围：`ContentPanel::search` 函数。
- 是否在需求范围内：是

---
## [4] 变更时间：2026-05-11 16:58:30

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
void ContentPanel::loadCategory(int categoryId) {
    m_isLoading = true;
    m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();
    emit dataSourceChanged("category");

    m_model->clear();

    std::vector<ArcMeta::ItemRepo::ItemRecord> allRecords;

    // 1. 加载子分类
    auto allCategories = CategoryRepo::getAll();
    for (const auto& cat : allCategories) {
        if (cat.parentId == categoryId) {
            ItemRepo::ItemRecord r;
            r.isCategory = true;
            r.categoryId = cat.id;
            allRecords.push_back(r);
        }
    }

    // 2. 加载文件
    auto itemRecords = ItemRepo::getRecordsInCategory(categoryId);
    allRecords.insert(allRecords.end(), itemRecords.begin(), itemRecords.end());

    // 工业级预读：对分类下的所有物理路径执行预读
    QStringList paths;
    for(const auto& r : itemRecords) paths << r.path;
    MetadataManager::instance().prefetchPaths(paths);

    m_model->setRecords(allRecords);

    m_isLoading = false;
    recalculateAndEmitStats();
    applyFilters();
}
```

### 修改后（After）
```cpp
void ContentPanel::loadCategory(int categoryId) {
    m_isLoading = true;
    m_viewStack->show();
    if (m_textPreview) m_textPreview->hide();
    if (m_imagePreview) m_imagePreview->hide();
    emit dataSourceChanged("category");

    m_model->clear();

    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, categoryId]() {
        std::vector<ArcMeta::ItemRepo::ItemRecord> allRecords;

        // 1. 加载子分类
        auto allCategories = CategoryRepo::getAll();
        for (const auto& cat : allCategories) {
            if (cat.parentId == categoryId) {
                ItemRepo::ItemRecord r;
                r.isCategory = true;
                r.categoryId = cat.id;
                allRecords.push_back(r);
            }
        }

        // 2. 加载文件
        auto itemRecords = ItemRepo::getRecordsInCategory(categoryId);
        allRecords.insert(allRecords.end(), itemRecords.begin(), itemRecords.end());

        // 工业级预读：对分类下的所有物理路径执行预读
        QStringList paths;
        for(const auto& r : itemRecords) paths << r.path;
        MetadataManager::instance().prefetchPaths(paths);

        QMetaObject::invokeMethod(qApp, [weakThis, allRecords]() {
            if (weakThis) {
                weakThis->m_model->setRecords(allRecords);
                weakThis->m_isLoading = false;
                weakThis->recalculateAndEmitStats();
                weakThis->applyFilters();
            }
        });
    });
}
```

### 变更说明
- 变更原因：将分类加载逻辑异步化，提升大数据量分类切换时的响应速度。
- 影响范围：`ContentPanel::loadCategory` 函数。
- 是否在需求范围内：是
