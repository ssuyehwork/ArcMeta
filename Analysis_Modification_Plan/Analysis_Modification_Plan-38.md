# 技术架构与逻辑优化方案：逻辑递归机制增强 (Analysis_Modification_Plan-38.md)

## 1. 系统现状确认：焦点指示线
经技术审计，确认 `MainWindow` 及其关联侧边栏已具备基于 `dataSourceChanged` 信号的“焦点指示线”机制：
- **信号源**：`ContentPanel::dataSourceChanged(const QString& source)` 已在 `loadCategory` 和 `loadDirectory` 中触发。
- **视觉实现**：`CategoryPanel` 与 `NavPanel` 均已实现 `setFocusHighlight(bool)`，且 `MainWindow` 已完成信号绑定。
**结论**：指示线系统完整，无需重复添加。本次方案将聚焦于缺失的“逻辑递归”控制能力。

---

## 2. 新增：双轨递归之“逻辑递归”按钮 (蓝色)

### 2.1 功能定义
在 `ContentPanel` 标题栏现有的 `m_btnLayers`（绿色，负责物理路径递归）左侧，新增 `m_btnLogicalLayers`（蓝色图标，负责分类逻辑递归）。

### 2.2 逻辑分工与持久化

| 特性 | 蓝色按钮 (逻辑递归) | 绿色按钮 (物理递归) |
| :--- | :--- | :--- |
| **适用场景** | 仅限“分类”视图（Category View）。 | 仅限“目录导航”视图（Nav View）。 |
| **持久化** | **是**：状态记录在 `AppConfig`。每次切换分类时，系统根据此状态决定是否执行深度递归查询。 | **否**：仅限当前物理会话，属于单次触发行为。 |
| **递归深度** | 递归穿透所有子分类，提取所有层级的关联文件。 | 递归穿透所有子文件夹，提取磁盘物理文件。 |

### 2.3 核心实现逻辑 (伪代码参考)

#### A. 按钮集成 (src/ui/ContentPanel.cpp) [已物理实现]
在 `initUi` 中，已在 `m_btnLayers` 之前成功插入新按钮：
- **变量名**：`m_btnLogicalLayers`
- **颜色**：`#3498db` (蓝色)
- **图标**：`layers`
- **位置**：`titleL->addWidget(m_btnLogicalLayers, 0, Qt::AlignVCenter);` 位于 `m_btnLayers` 之前。

目前点击逻辑已占位：
```cpp
connect(m_btnLogicalLayers, &QPushButton::clicked, [this]() {
    // TODO: 实现分类逻辑递归
});
```

#### B. 数据加载逻辑增强 (`loadCategory`)
在执行 `loadCategory(int categoryId)` 时，逻辑需根据蓝色按钮状态分支：
1.  **非递归模式**：调用 `CategoryRepo::getItemsInCategory(categoryId)`，仅加载当前分类项。
2.  **递归模式**：调用 `CategoryRepo::getItemsRecursive(categoryId)`，该接口已在底层实现，能够自动穿透加载所有子分类下的文件记录。

```cpp
// 2235 行附近逻辑改进建议
std::vector<CategoryItem> items;
if (m_btnLogicalLayers->isChecked()) {
    items = CategoryRepo::getItemsRecursive(categoryId);
} else {
    items = CategoryRepo::getItemsInCategory(categoryId);
}
```

### 2.4 互斥交互逻辑 (ToolTipOverlay 驱动)
为避免用户混淆，必须严格执行模式锁定：
- **逻辑模式点击绿色按钮**：弹出提示“物理递归不适用于分类视图”，并强制重置按钮状态。
- **物理模式下点击蓝色按钮**：弹出提示“逻辑递归仅适用于分类视图”，并引导用户使用右侧绿色按钮。

---

## 3. 总结
本方案在保留现有成熟“焦点指示线”架构的基础上，通过引入持久化的“逻辑递归”控制位，填补了分类系统在大规模层级展示上的功能空白，实现了逻辑与物理导航体验的完全对称。
