# 逻辑架构分析与代码修改方案 (Analysis_Modification_Plan-17.md)

## 1. ContentPanel 列表列架构重构 (8 列模式)

### 1.1 列索引定义
根据最新需求，列表列顺序及索引应调整如下：

| 索引 | 列名 | 建议宽度 | 最小值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| 0 | 名称 | 弹性 | 220px | 包含图标 |
| 1 | 状态 | 50px | 50px | 置顶/已录入图标 |
| 2 | 星级 | 120px | 120px | 包含禁止图标与 5 星 |
| 3 | 颜色标记 | 弹性 | 100px | 颜色圆点 |
| 4 | **标签** | 100px | 100px | **新增列** |
| 5 | 类型 | 80px | 80px | |
| 6 | 大小 | 80px | 80px | 右对齐 |
| 7 | 修改日期 | 150px | 150px | |

### 1.2 模型数据构建修改方案
在 `ContentPanel.cpp` 中所有涉及 `m_model->appendRow` 的地方，需插入新的标签项：

```cpp
// 示例修改逻辑
QList<QStandardItem*> row;
row << nameItem;              // 0: 名称
row << new QStandardItem(""); // 1: 状态
row << new QStandardItem(""); // 2: 星级
row << new QStandardItem(""); // 3: 颜色标记
row << new QStandardItem(data.meta.tags.join(", ")); // 4: 标签 (新增)
row << new QStandardItem(type_string); // 5: 类型
row << sizeItem;              // 6: 大小
row << mtimeItem;             // 7: 修改日期
```

---

## 2. TreeItemDelegate 高级交互方案

### 2.1 星级列禁止图标与运行逻辑
**视觉呈现：**
在索引为 2 的“星级列”中，绘制禁止图标 (`no_color`)。

**交互逻辑：**
1.  **Hitbox 检测**：在 `editorEvent` 中检测鼠标点击位置。如果落在禁止图标区域内，物理清空该项的星级评分。
2.  **联动更新**：清空评分后，调用 `MetadataManager::instance().setRating(path, 0)` 并更新 Model 数据。

### 2.2 绘制逻辑偏移 (索引映射)
由于插入了新列，`TreeItemDelegate::paint` 中的列判断逻辑需更新：
*   状态列：1
*   星级列：2
*   颜色列：3
*   标签列（可选自定义绘制）：4

---

## 3. 详细修改方案代码参考

### 3.1 设定列宽与最小值
**文件：** `src/ui/ContentPanel.cpp` -> `initListView()`

```cpp
auto* header = m_treeView->header();
header->setMinimumSectionSize(50); // 设置全局基础最小值

// 精准锁定各列尺寸
header->resizeSection(0, 250); // 名称
header->setMinimumSectionSize(0, 220); 

header->resizeSection(1, 50);  // 状态

header->resizeSection(2, 120); // 星级
header->setMinimumSectionSize(2, 120);

header->resizeSection(3, 100); // 颜色
// 2026-06-xx 物理特性：颜色标记列设为 Stretch 模式或保持较宽初始值以应对多色板
header->setSectionResizeMode(3, QHeaderView::Interactive); 

header->resizeSection(4, 100); // 标签
// 2026-06-xx 物理特性：由于标签可能较多，建议设为 Interactive 并给足初始空间

header->resizeSection(5, 80);  // 类型

header->resizeSection(6, 80);  // 大小

header->resizeSection(7, 150); // 修改日期
header->setMinimumSectionSize(7, 150);
```

### 3.2 TreeItemDelegate 绘制“禁止”图标
**文件：** `src/ui/TreeItemDelegate.h`

```cpp
// 在 paint 函数中处理 col == 2 (星级列)
if (col == 2) {
    int banW = 14;
    int gap = 4;
    int starSize = 14;
    int totalW = banW + gap + (5 * starSize);
    int startX = option.rect.left() + (option.rect.width() - totalW) / 2;
    
    // 1. 绘制禁止图标
    QRect banRect(startX, option.rect.top() + (option.rect.height() - banW)/2, banW, banW);
    UiHelper::getIcon("no_color", QColor("#888888"), banW).paint(painter, banRect);
    
    // 2. 绘制星级 (偏移 startX + banW + gap)
    int starsStartX = startX + banW + gap;
    // ... 原有星级绘制逻辑 ...
}
```

### 3.3 逻辑处理 (点击禁止图标)
需在 `TreeItemDelegate` 中覆写 `editorEvent`：

```cpp
bool TreeItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, 
                                 const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonPress && index.column() == 2) {
        QMouseEvent* mEvent = static_cast<QMouseEvent*>(event);
        // 计算 banRect (同 paint 逻辑)
        // ...
        if (banRect.contains(mEvent->pos())) {
             // 逻辑：清空评分
             QString path = index.model()->index(index.row(), 0).data(PathRole).toString();
             MetadataManager::instance().setRating(path.toStdWString(), 0);
             model->setData(index.model()->index(index.row(), 0), 0, RatingRole);
             return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
```
