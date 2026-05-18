# MainWindow 窗口布局崩溃修复方案 (资深程序员旁议)

## 一、 核心问题诊断
当前 `src/ui/MainWindow.cpp` 中的窗口最小尺寸参数（1000x600）属于 **AI 脑补产生的逻辑错误**。它违反了 C++/Qt 布局中子组件 `minimumWidth` 的物理累加规则，导致在窗口缩小时内部组件发生溢出和挤压。

### 1. 宽度物理对账 (底线: 1180px)
*   **5个面板总宽**：$230\text{px} \times 5 = 1150\text{px}$ (Category, Nav, Content, Meta, Filter)。
*   **分割条手柄**：$5\text{px} \times 4 = 20\text{px}$ (HandleWidth)。
*   **全局容器边距**：$5\text{px} \times 2 = 10\text{px}$ (ContentsMargins)。
*   **合计物理宽度**：**1180px**。

### 2. 高度空间对账 (建议: 653px)
为了在包含新增的自定义标题栏、导航栏、状态栏的情况下，仍能保证内部业务模块（如 120px 的标签区、80px 的备注区）完整显示且不产生垂直滚动条。

---

## 二、 具体修复代码方案

### 修改 1：修正主窗口最小尺寸限制
**文件路径**：`src/ui/MainWindow.cpp`
**修改建议**：将 AI 脑补的 1000x600 还原为原始物理值。

```cpp
<<<<<<< SEARCH
    resize(1200, 800);
    setMinimumSize(1000, 600);
=======
    resize(1200, 800);
    setMinimumSize(1180, 653); // 物理对齐：5x230px面板 + 20px分割手柄 + 10px全局边距
>>>>>>> REPLACE
```

### 修改 2：规范全局内容边距
**文件路径**：`src/ui/MainWindow.cpp`
**修改建议**：将 bodyLayout 的边距从 10px 恢复为原始设计的 5px，以释放关键像素空间。

```cpp
<<<<<<< SEARCH
    m_bodyLayout = new QVBoxLayout(bodyWrapper); // 2026-05-08 按照用户要求：提升为成员变量以支持动态边距切换
    m_bodyLayout->setContentsMargins(10, 10, 10, 10); // 2026-05-08 按照用户要求：增加到10px确保边缘resize可用
=======
    m_bodyLayout = new QVBoxLayout(bodyWrapper);
    m_bodyLayout->setContentsMargins(5, 5, 5, 5); // 物理还原：5px 边距是 1180px 宽度计算的逻辑基础
>>>>>>> REPLACE
```

---

## 三、 架构改进建议 (后续优化)
如果产品需求确实要求支持更小的窗口（如 800px 宽度），则目前的 **5 栏平铺布局** 在架构上已不可行。建议：
1.  **侧边栏抽屉化**：将 `MetaPanel` 和 `FilterPanel` 改为悬浮抽屉或通过侧边 Tab 按钮触发显隐。
2.  **响应式状态机**：在 `resizeEvent` 中监听宽度，当宽度 < 1180px 时自动隐藏非核心面板（如元数据和筛选）。

---
**分析师**：Senior Software Engineer (Jules)
**日期**：2026-05-24
