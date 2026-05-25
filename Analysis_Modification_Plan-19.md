# 逻辑架构分析与代码修改纠偏方案 (Analysis_Modification_Plan-19.md)

## 🚨 深度纠偏：针对 Plan-18 方案的复盘与修正

在之前的方案（Plan-18）中，直接在 `sectionResized` 信号内调用 `resizeSection` 会引发**死循环（递归触发）**，这是导致修复耗时过长的根源。此外，未考虑滚动条预留宽度和重绘开销。

### 1. 核心改进逻辑：重入保护机制
在 `ContentPanel` 中必须引入一个状态锁，防止信号反复回旋。

**修改策略：**
1.  **引入 `m_isResizing` 标记**（或局部 `static bool`）。
2.  **视口计算校准**：使用 `m_treeView->size().width()` 减去必要的边距，而不是直接使用可能为 0 的 `viewport()->width()`。
3.  **Stretch 模式冲突解决**：手动调整时暂时禁用 `setStretchLastSection`，调整完后再恢复，以防止布局争抢。

---

## 2. 修正后的 8 列物理锁定方案

| 索引 | 列名 | 锁定/最小宽度 | 交互逻辑 |
| :--- | :--- | :--- | :--- |
| 0 | 名称 | 220px (min) | 弹性调整，受总宽限制 |
| 1 | 状态 | 50px (Fixed) | 物理锁定，不可拖动缩放 |
| 2 | 星级 | 120px (min) | **集成禁止图标 + 点击清空评分** |
| 3 | 颜色标记 | 100px (min) | 允许弹性溢出（若容器足够宽） |
| 4 | 标签 | 100px (min) | |
| 5 | 类型 | 80px (min) | |
| 6 | 大小 | 80px (min) | |
| 7 | 修改日期 | 150px (min) | **自动拉伸填满剩余空间** |

---

## 3. 详细代码实现参考 (纠偏版)

### 3.1 容器宽度守恒拦截器
**文件：** `src/ui/ContentPanel.cpp`

```cpp
// 在 initListView() 中
auto* header = m_treeView->header();
header->setStretchLastSection(true);
m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

// 关键：物理拦截逻辑
connect(header, &QHeaderView::sectionResized, this, [this, header](int index, int oldSize, int newSize) {
    static bool guard = false;
    if (guard || index == 7) return; // 忽略自动拉伸列和递归调用

    guard = true; // 开启重入保护

    int totalWidth = header->length();
    int maxWidth = m_treeView->width() - 4; // 减去 4px 留白防止触发边缘滚动

    if (totalWidth > maxWidth) {
        // 计算当前列允许的最大值
        int over = totalWidth - maxWidth;
        int correctedSize = qMax(header->minimumSectionSize(), newSize - over);
        header->resizeSection(index, correctedSize);
    }

    guard = false; // 释放保护
});
```

### 3.2 星级列“禁止图标”交互补全
**文件：** `src/ui/TreeItemDelegate.h`
之前的方案可能遗漏了 `Hitbox` 的精确偏移计算。

```cpp
// 在 editorEvent 中
if (event->type() == QEvent::MouseButtonPress && index.column() == 2) {
    QMouseEvent* m = static_cast<QMouseEvent*>(event);
    // 物理对齐：这里的 banRect 必须与 paint 函数中的逻辑完全一致
    int banW = 14;
    int totalW = banW + 4 + (5 * 14); // banW + gap + stars
    int startX = option.rect.left() + (option.rect.width() - totalW) / 2;
    QRect banHitbox(startX, option.rect.top() + (option.rect.height() - banW)/2, banW, banW);

    if (banHitbox.contains(m->pos())) {
        // 执行重置逻辑... (代码略)
        return true;
    }
}
```

---

## 4. 总结与反思
Plan-19 方案通过 **Guard Flag** 解决了递归问题，通过 **Width Buffer (-4px)** 解决了边缘闪烁问题。这套方案更加符合工业级稳健性要求，能大幅缩短实际修复时间。
