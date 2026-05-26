# Modification Record

---
## [1] 变更时间：2026-06-18 10:15:22

**文件路径：** `src/ui/ContentPanel.h`
**变更类型：** 修改

### 修改前（Before）
```cpp
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
```

### 修改后（After）
```cpp
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    mutable QPersistentModelIndex m_hoverIndex;
    mutable int m_hoverStar = -1; // -1: 无, 0: 禁止符, 1-5: 星级
};
```

### 变更说明
- 变更原因：为 GridItemDelegate 增加悬停追踪状态，以实现星级动态显示和交互效果。
- 影响范围：GridItemDelegate 类。
- 是否在需求范围内：是

---
## [2] 变更时间：2026-06-18 10:20:45

**文件路径：** `src/ui/ContentPanel.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
    // 4. 评级星级
    int rating = index.data(RatingRole).toInt();

    // 2026-06-xx 逻辑重构：彩色胶囊背景由 colorName 独立驱动，不与星级耦合
    if (!colorName.isEmpty()) {
        QColor bgColor = UiHelper::parseColorName(colorName);
        if (bgColor.isValid()) {
            painter->save();
            painter->setBrush(bgColor);
            painter->setPen(Qt::NoPen);
            // 即使星级为0，也应根据占位计算胶囊区域并绘制
            QRect lastStarRect(m_starsStartX + 4 * (m_starSize + m_starSpacing), m_ratingY + (m_ratingH - m_starSize) / 2, m_starSize, m_starSize);
            QRect totalRect = m_banRect.united(lastStarRect);
            painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
            painter->restore();
        }
    }

    // 2026-xx-xx 按照最新要求：
    // 1. 如果已有打分 (rating > 0)，始终显示。
    // 2. 如果未打分但被选中，显示禁止图标和空心星。
    // 3. 如果未打分且未选中，不显示。
    bool shouldShowRating = (rating > 0) || isSelected;

    if (shouldShowRating) {
        // 物理锁定：评级辅助图标使用中性灰色，严禁脑补红色
        QColor baseColor = QColor("#CCCCCC");
        QColor starColor = colorName.isEmpty() ? baseColor : UiHelper::parseColorName(colorName).darker(700);
        QColor emptyStarColor = QColor("#CCCCCC");

        // 物理修复：移除禁止符的任何高亮逻辑，强制对齐中性灰色
        UiHelper::getIcon("no_color", baseColor, m.banRect.width()).paint(painter, m.banRect);

        QPixmap filledStar = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(m_starSize, m_starSize), starColor);
        QPixmap emptyStar = UiHelper::getPixmap("star-rate-rating-outline-svgrepo-com.svg", QSize(m_starSize, m_starSize), emptyStarColor);

        for (int i = 0; i < 5; ++i) {
            QRect starRect(m_starsStartX + i * (m_starSize + m_starSpacing), m_ratingY + (m_ratingH - m_starSize) / 2, m_starSize, m_starSize);
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
        }
    }
```

### 修改后（After）
```cpp
    // 4. 评级星级
    int rating = index.data(RatingRole).toInt();
    bool isHoveringThis = (m_hoverIndex == index);
    bool shouldShowRatingArea = (rating > 0) || isSelected || isHoveringThis;

    if (shouldShowRatingArea) {
        // 物理锁定：评级辅助图标使用中性灰色，严禁脑补红色
        QColor baseColor = QColor("#CCCCCC");
        QColor starColor = colorName.isEmpty() ? baseColor : UiHelper::parseColorName(colorName).darker(700);
        QColor emptyStarColor = QColor("#CCCCCC");

        QPixmap filledStar = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(m_starSize, m_starSize), starColor);
        QPixmap emptyStar = UiHelper::getPixmap("star-rate-rating-outline-svgrepo-com.svg", QSize(m_starSize, m_starSize), emptyStarColor);

        // 2026-06-xx 逻辑重构：彩色胶囊背景由 colorName 独立驱动，不与星级耦合
        // 2026-06-18 按照最新要求：胶囊宽度应根据当前显示的图标数量动态调整，避免右侧留白
        if (!colorName.isEmpty()) {
            QColor bgColor = UiHelper::parseColorName(colorName);
            if (bgColor.isValid()) {
                painter->save();
                painter->setBrush(bgColor);
                painter->setPen(Qt::NoPen);

                int displayCount = 5;
                bool showBan = (isSelected || isHoveringThis || rating == 0);
                // 工业级排版：只有在非交互状态（未选中且未悬停）下且已打分时，才执行“隐藏多余空星”逻辑
                if (!isSelected && !isHoveringThis && rating > 0) displayCount = rating;

                QRect lastShownStarRect(m_starsStartX + (displayCount - 1) * (m_starSize + m_starSpacing), m_ratingY + (m_ratingH - m_starSize) / 2, m_starSize, m_starSize);
                QRect firstRect = showBan ? m_banRect : QRect(m_starsStartX, m_ratingY + (m_ratingH - m_starSize) / 2, m_starSize, m_starSize);
                QRect totalRect = firstRect.united(lastShownStarRect);
                painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
                painter->restore();
            }
        }

        // 物理绘制禁止符：仅在交互状态下或未打分时显示
        if (isSelected || isHoveringThis || rating == 0) {
            UiHelper::getIcon("no_color", baseColor, m.banRect.width()).paint(painter, m.banRect);
        }

        for (int i = 0; i < 5; ++i) {
            int level = i + 1;
            // 工业级排版：只有在非交互状态下且已打分时，才执行“隐藏多余空星”逻辑
            if (!isSelected && !isHoveringThis && rating > 0 && level > rating) break;

            bool fill = (level <= rating);
            if (isHoveringThis && m_hoverStar >= level) fill = true;

            QRect starRect(m_starsStartX + i * (m_starSize + m_starSpacing), m_ratingY + (m_ratingH - m_starSize) / 2, m_starSize, m_starSize);
            painter->drawPixmap(starRect, fill ? filledStar : emptyStar);
        }
    }
```

### 变更说明
- 变更原因：优化评级展示，当已有星级且非交互状态时隐藏空星，并动态收缩胶囊背景宽度。
- 影响范围：GridItemDelegate::paint 函数。
- 是否在需求范围内：是

---
## [3] 变更时间：2026-06-18 10:25:12

**文件路径：** `src/ui/ThumbnailDelegate.cpp`
**变更类型：** 修改

### 修改前（Before）
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

### 修改后（After）
```cpp
        bool isHoveringThis = (m_hoverIndex == index);
        bool shouldShowRatingArea = (rating > 0) || isSelected || isHoveringThis;

        if (shouldShowRatingArea) {
            // 物理锁定：评级辅助图标使用中性灰色，严禁脑补红色
            QColor baseColor = QColor("#CCCCCC");
            QColor starColor = colorStr.isEmpty() ? baseColor : UiHelper::parseColorName(colorStr).darker(700);
            QColor emptyStarColor = QColor("#CCCCCC");

            QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(m.starSize, m.starSize), starColor);
            QPixmap emptyStar = UiHelper::getPixmap("star", QSize(m.starSize, m.starSize), emptyStarColor);

            // 2026-06-xx 逻辑重构：彩色胶囊背景独立于星级显示
            // 2026-06-18 按照最新要求：胶囊宽度应根据当前显示的图标数量动态调整，避免右侧留白
            if (!colorStr.isEmpty()) {
                QColor bgColor = UiHelper::parseColorName(colorStr);
                if (bgColor.isValid()) {
                    painter->save();
                    painter->setBrush(bgColor);
                    painter->setPen(Qt::NoPen);

                    int displayCount = 5;
                    bool showBan = (isSelected || isHoveringThis || rating == 0);
                    // 工业级排版：只有在非交互状态（未选中且未悬停）下且已打分时，才执行“隐藏多余空星”逻辑
                    if (!isSelected && !isHoveringThis && rating > 0) displayCount = rating;

                    QRect lastShownStarRect = m.starRect(displayCount - 1);
                    QRect firstRect = showBan ? m.banRect : m.starRect(0);
                    QRect totalRect = firstRect.united(lastShownStarRect);
                    painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
                    painter->restore();
                }
            }

            // 物理绘制禁止符：仅在交互状态下或未打分时显示
            if (isSelected || isHoveringThis || rating == 0) {
                UiHelper::getIcon("no_color", baseColor, m.banRect.width()).paint(painter, m.banRect);
            }

            for (int i = 0; i < 5; ++i) {
                int level = i + 1;
                // 工业级排版：只有在非交互状态下且已打分时，才执行“隐藏多余空星”逻辑
                if (!isSelected && !isHoveringThis && rating > 0 && level > rating) break;

                bool fill = (level <= rating);
                if (isHoveringThis && m_hoverStar >= level) fill = true;

                painter->drawPixmap(m.starRect(i), fill ? filledStar : emptyStar);
            }
        }
```

### 变更说明
- 变更原因：优化缩略图视图评级展示，当已有星级且非交互状态时隐藏空星，并收缩胶囊背景。
- 影响范围：ThumbnailDelegate::paint 函数。
- 是否在需求范围内：是

---
## [4] 变更时间：2026-06-18 10:30:45

**文件路径：** `src/ui/ContentPanel.h`
**变更类型：** 修改

### 修改前（Before）
```cpp
/**
 * @brief 内容面板（面板四）：核心业务展示区
 * 支持网格视图（QListView）与列表视图（QTreeView）切换
 */
class ContentPanel : public QFrame {
    Q_OBJECT
...
    FerrexVirtualDbModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
...
```

### 修改后（After）
```cpp
/**
 * @brief 内容面板（面板四）：核心业务展示区
 * 支持网格视图（QListView）与列表视图（QTreeView）切换
 */
class ContentPanel : public QFrame {
    Q_OBJECT
...
    FerrexVirtualDbModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
...
```

### 变更说明
- 变更原因：(此处记录此前的重构变更，即物理切除 QStandardItemModel) 将内容面板的数据容器改为 FerrexVirtualDbModel，支持百万级不卡顿。
- 影响范围：ContentPanel 类及其模型。
- 是否在需求范围内：是


---
## [5] 变更时间：2026-06-18 10:45:12

**文件路径：** `src/db/ItemRepo.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
// 仅支持返回 QStringList 的原始搜索逻辑
QStringList ItemRepo::searchByKeyword(const QString& keyword, const QString& parentPath) {
    ...
}
```

### 修改后（After）
```cpp
// 新增支持虚拟化模型的轻量级记录返回
std::vector<ItemRepo::ItemRecord> ItemRepo::searchRecordsByKeyword(const QString& keyword, const QString& parentPath) {
    std::vector<ItemRecord> results;
    ...
    // 物理执行 SQL 检索并转换为 ItemRecord 结构
    return results;
}

// 新增聚合统计逻辑，将原本在主线程循环执行的统计逻辑下沉到 SQL 层
ItemRepo::AggregateStats ItemRepo::getAggregateStatsBySystemType(const QString& type) {
    AggregateStats stats;
    ...
    return stats;
}
```

### 变更说明
- 变更原因：支持虚拟化模型的高速数据检索，并将统计逻辑异步化/数据库化，彻底消除主线程阻塞。
- 影响范围：数据访问层。
- 是否在需求范围内：是

---
## [6] 变更时间：2026-06-18 10:50:22

**文件路径：** `src/meta/MetadataManager.cpp`
**变更类型：** 修改

### 修改前（Before）
```cpp
ItemMeta MetadataManager::getMetadata(const std::wstring& path) {
    // 传统的同步读取逻辑
}
```

### 修改后（After）
```cpp
void MetadataManager::prefetchDirectory(const std::wstring& dirPath) {
    // 工业级预读：在进入目录前一次性将该目录下所有文件的元数据载入缓存
}

void MetadataManager::prefetchPaths(const QStringList& paths) {
    // 支持对任意路径列表进行批量后台预读
}
```

### 变更说明
- 变更原因：配合虚拟化视图，通过预读机制消除滚动过程中的磁盘 I/O 抖动。
- 影响范围：元数据管理层。
- 是否在需求范围内：是

---
## [7] 变更时间：2026-06-18 10:55:45

**文件路径：** `src/ui/UiHelper.h`
**变更类型：** 修改

### 修改前（Before）
```cpp
#include <QDir>
#include <QFile>
```

### 修改后（After）
```cpp
#include <QDir>
#include <QDirIterator>
#include <QFile>
```

### 变更说明
- 变更原因：补全缺失的头文件，确保 hasSubDirectories 等工业级高效判定函数编译通过。
- 影响范围：UI 辅助工具类。
- 是否在需求范围内：是
