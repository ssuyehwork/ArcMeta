# Analysis_Modification_Plan-62: MetaPanel 布局溢出诊断与重构方案

## § 1 布局溢出根因诊断

通过对 `MetaPanel` 源码的审计，确认视觉溢出是由以下非工业级布局逻辑导致的：

### 1.1 PaletteCapsule 的强制物理宽度
- **代码位置**：`MetaPanel.cpp` (约行 69)
```cpp
int w = m_padding * 2 + (int)palette.size() * m_dotSize + ((int)palette.size() - 1) * m_spacing;
setFixedSize(w, 28);
```
- **问题分析**：`PaletteCapsule` 根据色点数量计算出一个绝对像素宽度，并使用 `setFixedSize` 锁定。这种做法完全无视了 Qt 布局系统的弹性约束。当色点达到一定数量时，计算出的宽度会超过 `MetaPanel` 的容器宽度，导致组件横向“刺破”面板。

### 1.2 容器约束失效
- **代码位置**：`MetaPanel.cpp` (约行 324-328)
- **问题分析**：虽然使用了 `QHBoxLayout` 和 `addStretch()`，但由于 `m_paletteCapsule` 内部设置了 `FixedSize`，布局管理器无法对其进行收缩或截断，导致包装器也随之溢出。

---

## § 2 工业级重构方案：永不溢出的自适应布局

为了实现“无论多窄都不溢出”且“保持 10px 边距”的要求，必须废弃固定的物理尺寸计算，转而采用响应式布局或绘制逻辑。

### 2.1 方案 A：引入流式布局（FlowLayout）封装调色盘
- **修改建议**：
    1. 将 `PaletteCapsule` 从单体组件改为容器组件，内部每个色点作为独立的子 Widget。
    2. 使用 `MetaPanel.h` 中现有的 `FlowLayout` 来排列这些色点。
- **优点**：当宽度不足时，色点会自动换行显示，绝对不会超出边界。

### 2.2 方案 B：响应式缩略逻辑（视觉优化首选）
- **修改建议**：
    1. **废弃 `setFixedSize`**：将 `PaletteCapsule` 的 `sizePolicy` 设置为 `Expanding`，并移除硬编码的宽度设置。
    2. **重写 `paintEvent`**：
        - 实时计算可用宽度：`int availableW = width() - m_padding * 2;`
        - 动态调整绘制：如果总宽度超过 `availableW`，则通过缩减间距或仅绘制可见范围内的色点，并添加渐变消隐效果。
    3. **确保边距**：确保 `m_containerLayout` 的 `setContentsMargins(10, 10, 10, 10)` 始终生效，且父容器不被子组件强行撑开。

### 2.3 方案 C：布局约束加固
- **修改建议**：
    1. 为 `MetaPanel` 内的所有 `ElasticEdit` 和自定义组件设置合理的 `maximumWidth` 或确保其能响应 `sizeHint` 的缩减。
    2. 在 `MetaPanel` 的容器层级强制限制子组件的最大宽度不超过 `container->width() - 20`（双侧 10px 边距）。

---

## § 3 总结
截图中的溢出是由于开发者为了视觉表现使用了硬编码的宽度计算，却忽略了 UI 的边界情况。典型的修复方案是**将控制权从组件自身移交给布局系统**。建议采用方案 A 或 B，以保证在任何宽度下调色盘都能优雅地待在 10px 边距之内。
