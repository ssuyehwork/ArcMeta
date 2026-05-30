# 分析计划 #53 ｜ 搜索框宽度锁定与布局加固方案

### § 1 现状分析 (Current Analysis)

**1.1 代码现状调查**
目前在 `src/ui/MainWindow.cpp` 第 774 行附近：
```cpp
m_searchEdit->setFixedWidth(230);
```
虽然当前代码已显式调用了 `setFixedWidth(230)`，但根据用户反馈，在实际运行或特定的 UI 缩放场景下，该宽度表现不符合预期。

**1.2 “AI 脑补”风险点诊断**
- **布局压缩 (Layout Compression)**：如果 `m_searchEdit` 被放置在一个 `QHBoxLayout` 中，且同级的地址栏或导航按钮拥有更高的伸缩系数（Stretch Factor），当窗口缩小到一定程度时，布局管理器可能会忽略 `setFixedWidth` 的软性约束，尝试强行压缩所有组件以适应屏幕。
- **样式表冲突 (QSS Overlap)**：如果全局样式表（QSS）中定义了 `QLineEdit { width: ... }` 或 `min-width`，它会覆盖 C++ 代码中的 `setFixedWidth` 设置。
- **DPI 缩放处理**：在不同 DPI 下，230 像素如果未经过 `StyleHelper::dpiScaled(230)` 处理，在 4K 屏幕上会显得过窄。

### § 2 修改方案 (Modification Plan)

为了实现真正“不可动摇”的 230 像素宽度，建议采取以下三重锁定方案：

#### 方案 A：布局优先级锁定 (Layout Constraint)
在添加组件到布局时，显式指定伸缩因子为 0，并设置 SizePolicy。
```cpp
// 在 MainWindow.cpp 中
m_searchEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
m_navBarLayout->addWidget(m_searchEdit, 0); // 0 表示不参与布局伸缩
```

#### 方案 B：样式表强制锁定 (QSS Force)
通过 QSS 锁定最小和最大宽度，这是优先级最高的约束方式。
```cpp
m_searchEdit->setStyleSheet(
    "QLineEdit { "
    "  min-width: 230px; "
    "  max-width: 230px; "
    "  background: #1E1E1E; "
    "  border-radius: 4px; "
    "}"
);
```

#### 方案 C：DPI 感知适配 (DPI Awareness)
确保 230 像素在不同分辨率下视觉比例一致。
```cpp
int targetWidth = StyleHelper::dpiScaled(230);
m_searchEdit->setFixedWidth(targetWidth);
```

### § 3 实施建议
建议采用 **方案 B**，因为它不仅锁定了物理像素，还统一了视觉样式（圆角与背景），能有效防止布局管理器的二次计算。

---
**旁观者/分析师：** Jules (资深程序员)
**状态：** 方案已就绪，等待非代码修改类操作确认。
