# 架构分析与 UI 规范方案 (Analysis_Modification_Plan-19)

本方案旨在明确 FERREX 应用程序中地址栏与搜索框的 UI 视觉设计标准，特别是圆角像素的设计规范，并分析其在逻辑架构中的实现方式。

---

## 1. 圆角设计像素值分析

经过对 `src/ui/` 目录下核心 UI 组件代码的深度扫描，确认地址栏与搜索框均采用了统一的现代圆角风格，具体数值如下：

| 组件名称 | 关键对象 | 圆角像素 (Corner Radius) | 样式定义位置 |
| :--- | :--- | :--- | :--- |
| **地址栏** | `m_addressContainer` | **6px** | `src/ui/AddressBar.cpp` |
| **搜索框** | `m_searchEdit` | **6px** | `src/ui/MainWindow.cpp` |

### 详细代码解析

#### 1.1 地址栏 (Address Bar)
地址栏采用了“容器化”设计逻辑。在 `AddressBar.cpp` 中，所有的视觉特征都绑定在 `AddressContainer` 这个 QWidget 插件上：

```cpp
// src/ui/AddressBar.cpp
m_addressContainer->setStyleSheet(
    "QWidget#AddressContainer { background: #1E1E1E; border: 1px solid #333333; border-radius: 6px; }"
    "QWidget#AddressContainer[focused='true'] { border: 1px solid #3498db; }"
);
```

此外，地址栏右侧的刷新按钮也进行了边缘圆角适配，以确保与外层容器完美贴合：
```cpp
// src/ui/AddressBar.cpp
m_btnRefresh->setStyleSheet(
    "QPushButton { ... border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
);
```

#### 1.2 搜索框 (Search Box)
搜索框的样式在 `MainWindow.cpp` 的 `initToolbar` 函数中定义。它遵循了与地址栏完全一致的圆角标准，以维持顶部导航栏的视觉统一性：

```cpp
// src/ui/MainWindow.cpp
m_searchEdit->setStyleSheet(QString(
    "QLineEdit { background: %1; border: 1px solid %2;"
    "  border-radius: 6px;"
    "  color: %3; padding-left: 5px; }"
    "QLineEdit:focus { border: 1px solid %4; }"
).arg(...));
```

---

## 2. 逻辑架构设计原则

地址栏与搜索框的设计体现了 FERREX 项目的以下架构理念：

1.  **视觉对等原则**：作为顶部导航栏中最重要的两个输入/展示组件，通过相同的 `6px` 圆角和 `32px` 固定高度，建立视觉上的物理对等感。
2.  **容器化封装**：地址栏不再是简单的输入框，而是一个包含面包屑导航、文本编辑和刷新动作的复合容器（`AddressContainer`）。这种设计允许在不破坏圆角外轮廓的情况下，内部动态切换不同的子组件（`m_pathStack`）。
3.  **动态交互反馈**：圆角边框的颜色会根据 `focused` 属性动态变化（从 `#333333` 变为 `#3498db`）。在地址栏中，这通过手动驱动 `style()->polish()` 来实现，弥补了 QSS 对 `focus-within` 伪类支持的不足。

---

## 3. 修改建议与规范

若未来需要对圆角进行全局调整，建议遵循以下方案：

*   **集中化管理**：目前的 QSS 字符串散落在各 `.cpp` 文件中。建议将其迁移至全局 `style.qss` 资源文件或 `StyleLibrary.h` 中定义的常量。
*   **物理红线**：地址栏与搜索框的圆角必须保持同步。若搜索框改为直角（0px），地址栏容器亦应同步调整，否则会破坏顶部工具栏的视觉平衡。
*   **高分屏适配**：当前的 `6px` 是基于 96 DPI 的硬编码值。在进行高 DPI (Retina) 适配时，应考虑使用 `UiHelper` 将像素值乘以缩放系数。
