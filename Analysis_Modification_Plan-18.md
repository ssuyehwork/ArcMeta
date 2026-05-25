# 逻辑架构分析与代码修改方案 (Analysis_Modification_Plan-18.md)

## 1. 列表列宽边界约束方案 (核心：禁止溢出)

### 1.1 技术分析
为了实现“手动调整列宽时不超出 ContentPanel 范围”，必须打破 Qt 默认的“横向滚动条”逻辑，代之以“视口内守恒”逻辑。

### 1.2 建议实现策略
1.  **开启末尾自动拉伸**：调用 `header->setStretchLastSection(true)`。这确保了当其他列宽度较小时，最后一列会自动填满剩余空间。
2.  **动态溢出拦截**：
    -   监听 `sectionResized(int index, int oldSize, int newSize)` 信号。
    -   在回调中计算当前表头的总长度 (`length()`)。
    -   如果 `length() > viewport()->width()`，则物理阻止进一步扩大，或者通过 `resizeSection` 将该列强制设置回允许的最大宽度。
3.  **禁用横向滚动条**：明确调用 `m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)`，从视觉和交互上彻底禁绝溢出。

---

## 2. 8 列架构与物理锁定参数

基于最新需求，所有列的初始与约束参数如下：

| 索引 | 列名 | 最小值 | 初始/目标值 | 行为约束 |
| :--- | :--- | :--- | :--- | :--- |
| 0 | 名称 | 220px | 300px | 交互调整 |
| 1 | 状态 | 50px | 50px | **锁定宽度** |
| 2 | 星级 | 120px | 120px | 包含禁止图标 |
| 3 | 颜色标记 | 100px | 100px | 弹性空间 |
| 4 | 标签 | 100px | 100px | 弹性空间 |
| 5 | 类型 | 80px | 80px | |
| 6 | 大小 | 80px | 80px | |
| 7 | 修改日期 | 150px | 150px | **Stretch 模式** |

---

## 3. 详细修改方案代码参考

### 3.1 ContentPanel 初始化配置
**文件：** `src/ui/ContentPanel.cpp`

```cpp
void ContentPanel::initListView() {
    // ...
    auto* header = m_treeView->header();

    // 1. 物理禁绝横向滚动，确保容器边界不被破坏
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 2. 设定最后一列自动填充，防止容器内出现空白
    header->setStretchLastSection(true);

    // 3. 设定 8 列初始参数
    header->resizeSection(0, 300); header->setMinimumSectionSize(0, 220);
    header->resizeSection(1, 50);  header->setMinimumSectionSize(1, 50);
    header->resizeSection(2, 120); header->setMinimumSectionSize(2, 120);
    header->resizeSection(3, 100); header->setMinimumSectionSize(3, 100);
    header->resizeSection(4, 100); header->setMinimumSectionSize(4, 100);
    header->resizeSection(5, 80);  header->setMinimumSectionSize(5, 80);
    header->resizeSection(6, 80);  header->setMinimumSectionSize(6, 80);
    header->resizeSection(7, 150); header->setMinimumSectionSize(7, 150);

    // 4. 边界联动：监听缩放以防止总宽溢出
    connect(header, &QHeaderView::sectionResized, this, [this, header](int index, int oldSize, int newSize) {
        if (index == 7) return; // 忽略拉伸列

        int totalWidth = header->length();
        int viewportWidth = m_treeView->viewport()->width();

        if (totalWidth > viewportWidth) {
            // 物理拦截：如果超出视口，强制回滚宽度
            int allowedWidth = newSize - (totalWidth - viewportWidth);
            header->resizeSection(index, qMax(allowedWidth, header->sectionSizeHint(index)));
        }
    });
}
```

### 3.2 TreeItemDelegate 交互补全 (星级清空)
**文件：** `src/ui/TreeItemDelegate.h`
*   需在 `editorEvent` 中加入对 `index.column() == 2` 且命中禁止图标区域的评分重置逻辑。

---

## 4. 总结
通过以上“三位一体”（Stretch 模式、滚动条封印、信号拦截）的方案，可以确保 `ContentPanel` 的列表视图在任何操作下都严格保持在容器物理边界内，增强“工业级”界面的稳固感。
