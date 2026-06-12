# 架构分析与 UI 规范方案 (Analysis_Modification_Plan-20)

本方案旨在明确 FERREX 地址栏历史记录面板（AddressHistoryPanel）的 UI 视觉设计标准及数据存储逻辑限制。

---

## 1. 圆角设计像素值分析

地址栏历史面板作为一个悬浮弹出组件（Popup），采用了比主输入框略大的圆角设计，以增强悬浮感和视觉柔和度。

| 组件名称 | 关键对象 | 圆角像素 (Corner Radius) | 样式定义位置 |
| :--- | :--- | :--- | :--- |
| **地址历史面板** | `#AddressHistoryPanel` | **8px** | `src/ui/AddressHistoryPanel.cpp` |
| **单条历史行** | `historyRow` | **4px** | `src/ui/AddressHistoryPanel.cpp` |

### 详细代码解析

在 `src/ui/AddressHistoryPanel.cpp` 的构造函数中，明确定义了面板的外边框圆角：

```cpp
setStyleSheet(
    "#AddressHistoryPanel {"
    "  background-color: #252526;"
    "  border: 1px solid #444444;"
    "  border-radius: 8px;" // 面板主圆角
    "}"
);
```

此外，内部每一行历史记录在悬停时显示的背景也具有圆角：
```cpp
row->setStyleSheet(
    "QWidget#historyRow { background: transparent; border-radius: 4px; }"
    "QWidget#historyRow:hover { background: #2A2A2A; }"
);
```

---

## 2. 记录数量上限与限制分析

针对“该面板支持多少记录”的问题，系统在逻辑层实施了硬性限制，并非无限增长。

### 2.1 存储上限
在 `src/ui/AddressBar.cpp` 的 `saveToHistory` 函数中，历史记录被限制为最多 **15 条**：

```cpp
// src/ui/AddressBar.cpp
void AddressBar::saveToHistory(const QString& path) {
    if (path.isEmpty() || path == "computer://" || path.startsWith("分类: ")) return;
    QStringList history = AppConfig::instance().getValue("AddressBar/History").toStringList();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > 15) history.removeLast(); // 物理限制：上限 15 条
    AppConfig::instance().setValue("AddressBar/History", history);
}
```

### 2.2 限制原因分析
1.  **交互效率**：15 条记录可以在不出现滚动条的情况下覆盖用户最常用的高频路径。
2.  **视觉美感**：过长的历史列表会导致面板超出主窗口底部边缘，影响视觉一致性。
3.  **持久化性能**：使用 `AppConfig` (QSettings) 存储，限制数量可以确保配置文件的读取速度始终保持在毫秒级。

---

## 3. 架构建议

*   **配置化限制**：目前 15 条记录是硬编码在代码中的。如果未来需要允许用户自定义，建议将其迁移至 `AppConfig` 作为配置项。
*   **视觉对齐**：注意到搜索框的历史面板（SearchHistoryPanel）圆角同样是 `8px`，但记录上限为 `10条`。为保证用户体验的一致性，建议未来将两者的上限数值统一（如全部设为 15）。
*   **清理逻辑**：面板右上角提供了“全部清除”按钮，这在物理上会直接清空 `AppConfig` 中的对应键值，符合用户对隐私清理的需求。
