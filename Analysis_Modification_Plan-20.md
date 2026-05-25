# 工业级 UI 交互架构分析方案 (Analysis_Modification_Plan-20.md)

## 1. 核心意图：物理守恒与递归安全

本方案致力于解决 `ContentPanel` 列表视图在手动调整列宽时产生的“溢出”和“递归死锁”问题。核心逻辑是：**容器宽度是绝对红线，一切调整必须在视口内完成自平衡。**

### 1.1 动态边界防御逻辑
为了确保调整不超出容器，且不引发死循环，建议采用“三段式”防御：

1.  **主动拦截 (Event Filter)**：在 `QHeaderView` 的鼠标移动事件中提前预判宽度。
2.  **信号防抖 (Signal Guard)**：使用 `m_isResizing` 布尔锁。
3.  **动态补偿 (Stretch Compensation)**：
    -   设定最后一列（修改日期）为 `Stretch`。
    -   调整前几列时，如果总宽即将溢出，则强制压缩“名称”列或锁定当前列的最大增长值。

---

## 2. 8 列架构物理参数锁定 (2026-06-16 最终校准)

| 索引 | 列名 | 锁定/最小宽度 | 绘制逻辑 & 运行意图 |
| :--- | :--- | :--- | :--- |
| 0 | 名称 | 220px (min) | **首要弹性列**。当其他列扩张时，此列优先收缩以保持总宽。 |
| 1 | 状态 | 50px (Fixed) | 置顶/录入图标。 |
| 2 | 星级 | 120px (Fixed) | **交互核心**。左侧 20px 留给“禁止图标”，右侧 100px 绘制星级。 |
| 3 | 颜色标记 | 100px (min) | 颜色圆点显示。 |
| 4 | 标签 | 100px (min) | 文本显示。 |
| 5 | 类型 | 80px (Fixed) | 固定分类文本。 |
| 6 | 大小 | 80px (Fixed) | 右对齐。 |
| 7 | 修改日期 | 150px (min) | **次要弹性列**。作为 `Stretch` 填充项，吸收容器大小变化。 |

---

## 3. 稳健性代码实现方案 (纠偏版)

### 3.1 宽度守恒拦截器 (解决递归与溢出)
**文件：** `src/ui/ContentPanel.cpp`

```cpp
// 逻辑：在 QHeaderView 级别建立物理边界
void ContentPanel::initListView() {
    auto* header = m_treeView->header();
    header->setStretchLastSection(true);
    header->setCascadingSectionResizes(false); // 关键：禁止级联缩放以保持控制力

    connect(header, &QHeaderView::sectionResized, this, [this, header](int index, int oldSize, int newSize) {
        static bool guard = false;
        if (guard || index == 7) return;

        guard = true;

        // 计算当前所有列物理宽度总和
        int currentTotal = header->length();
        int maxAvailable = m_treeView->viewport()->width();

        if (currentTotal > maxAvailable) {
             // 意图：不允许产生滚动条。如果当前列变宽导致溢出，则强制回滚。
             int allowed = newSize - (currentTotal - maxAvailable);
             header->resizeSection(index, qMax(header->minimumSectionSize(index), allowed));
        }

        guard = false;
    });
}
```

### 3.2 星级清空逻辑 (运行逻辑闭环)
**文件：** `src/ui/TreeItemDelegate.h`

```cpp
// 在 editorEvent 中处理点击“禁止”图标
if (event->type() == QEvent::MouseButtonPress && index.column() == 2) {
    QMouseEvent* me = static_cast<QMouseEvent*>(event);

    // 精准 Hitbox：禁止图标固定在列起始偏移 5px 处，大小 16px
    QRect banHitbox(option.rect.left() + 5, option.rect.top() + (option.rect.height() - 16)/2, 16, 16);

    if (banHitbox.contains(me->pos())) {
        QString path = index.model()->index(index.row(), 0).data(PathRole).toString();
        // 核心意图：一键物理回滚
        MetadataManager::instance().setRating(path.toStdWString(), 0);
        model->setData(index.model()->index(index.row(), 0), 0, RatingRole);
        return true;
    }
}
```

---

## 4. 总结
本方案的核心在于**“拒绝脑补、锁定物理边界”**。通过重入保护和精准的像素 Hitbox 检测，确保程序员在按照此方案实施时，能够直接避开 Qt 布局的常见陷阱，实现您所期望的“工业级”稳健界面。
