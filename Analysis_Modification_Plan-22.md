# Analysis & Modification Plan - UI 交互规范 (圆角篇)

## 1. 现状分析 (Current State)
通过对核心 UI 组件的样式表 (QSS) 审计，确定了当前系统的圆角层级规范。

### 1.1 核心输入组件
*   **地址栏 (AddressBar):** 
    - 外部容器 `#AddressContainer` 定义为 `border-radius: 6px`。
    - 内部刷新按钮等组件配合容器形状，保持了视觉的一致性。
*   **搜索框 (QLineEdit):**
    - 全局 `QLineEdit` 样式定义为 `border-radius: 6px`。
    - 搜索框在聚焦时通过 `border: 1px solid PrimaryBlue` 强化轮廓，但圆角保持不变。

### 1.2 弹出与浮动面板
*   **历史记录面板 (HistoryPanel):**
    - `SearchHistoryPanel` 和 `AddressHistoryPanel` 的外层边框为 `border-radius: 8px`。
    - 这种较大的圆角设计旨在产生“悬浮感”和“层级感”。
*   **项目行 (Item Row):**
    - 面板内部的每一行记录 (`historyRow`) 在悬停时显示的背景圆角为 `border-radius: 4px`。

## 2. 逻辑架构建议 (Architectural Recommendations)
为了保持 UI 的长期严谨性，建议将这些数值常量化：

| 级别 | 像素值 | 应用场景 |
| :--- | :--- | :--- |
| **Small** | 3px | 滚动条滑块 (ScrollBar handle) |
| **Medium** | 4px | 按钮 (QPushButton)、列表项悬停态 |
| **Large** | 6px | 输入框 (QLineEdit)、主容器、地址栏 |
| **X-Large** | 8px | 弹出菜单 (QMenu)、悬浮面板 (Floating Panel) |

## 3. 实施方案 (Implementation Plan)
无需立即修改代码，但在后续开发中应遵循以下原则：
1. **统一 QSS 定义：** 避免在不同的 `.cpp` 文件中硬编码像素值，应优先使用 `StyleLibrary` 或全局 QSS 资源文件。
2. **DPI 适配：** 在高分屏下，这些像素值应配合 `Qt::AA_EnableHighDpiScaling` 自动缩放，无需手动调整。
3. **颜色配合：** 6px 的圆角在深色模式下，必须配合 `1px` 的 `BorderColor` (#333333) 才能凸显其“物理切割感”。
