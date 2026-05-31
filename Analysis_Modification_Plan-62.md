# Analysis_Modification_Plan-62: MetaPanel 布局溢出与间距异常深度诊断

## § 1 布局溢出根因诊断 (2026-06-01 更新)

根据最新的视觉反馈，即使实施了初步的 10px 边距限制，系统在加载长文本（如长路径或超长文件名）时仍会出现横向溢出。

### 1.1 ElasticEdit 的宽度扩张风险
- **代码位置**：`MetaPanel.cpp` 中的 `ElasticEdit` 实现
- **问题分析**：`ElasticEdit` 继承自 `QPlainTextEdit`。虽然开启了 `WidgetWidth` 换行模式，但在 `QScrollArea` 环境下，如果子组件的 `sizeHint` 报告了一个巨大的宽度，`QScrollArea` 的内容小部件会尝试扩张以匹配该宽度，导致横向滚动条出现或布局被刺破。

### 1.2 QLabel (lblPath) 的换行失效
- **代码位置**：`addInfoRow` 函数
- **问题分析**：`lblPath` 虽然设置了 `setWordWrap(true)`，但在没有明确最大宽度约束的情况下，Qt 的布局引擎往往会优先选择增加宽度而非换行，尤其是当它处于一个高度可变的 `QVBoxLayout` 中时。

### 1.3 标记 ① 处垂直间距过大的原因
- **代码位置**：`initUi` (行 336) 与 `addInfoRow` (行 433)
- **技术推演**：
    1. `m_containerLayout` 设置了 `setSpacing(12)`。
    2. `addInfoRow` 内部为每个 `row` 小部件设置了 `setContentsMargins(0, 4, 0, 4)`。
    3. **累加效应**：两个相邻行之间的物理间距 = 行N底部边距(4px) + 布局间距(12px) + 行N+1顶部边距(4px) = **20px**。这在紧凑的元数据面板中显得过于空旷，产生了视觉上的断裂感。

---

## § 2 工业级重构方案：极致约束与视觉微调

### 2.1 引入“硬性”宽度截断逻辑
- **修改建议**：
    1. 在 `MetaPanel` 中重写 `resizeEvent`。
    2. 实时计算有效内容宽度：`int maxContentW = this->width() - 20;` (预留左右 10px)。
    3. 强制遍历 `m_container` 的子组件，显式调用 `setMaximumWidth(maxContentW)`。
    4. 特别针对 `ElasticEdit`：确保其在文本变化触发 `adjustHeight` 时，不会因为宽度过窄导致高度计算陷入死循环，应使用 `document()->setTextWidth(maxContentW)`。

### 2.2 优化 addInfoRow 的视觉密度
- **修改建议**：
    1. **压缩边距**：将 `addInfoRow` 中的 `rl->setContentsMargins(0, 2, 0, 2)`。
    2. **按需间距**：将 `m_containerLayout->setSpacing(8)`。
    3. **分组对齐**：为详情网格部分引入独立的子布局，并降低该子布局内的 `spacing`，使其在视觉上与上方的输入框区域形成区分。

### 2.3 路径显示方案优化
- **修改建议**：
    1. 对 `lblPath` 使用 `ElideMode`（省略号截断）而非强制换行，或者提供点击复制功能。
    2. 在 `paintEvent` 中处理超长文本，确保其不会撑开容器。

---

## § 3 总结
目前的布局问题属于 Qt 布局系统中典型的“约束传播失效”。当容器嵌套在 `QScrollArea` 中时，子组件必须具备主动感知并顺从父级宽度的意识。通过强制限制 `maximumWidth` 并微调复合间距，可以彻底解决溢出并消除标记 ① 处的视觉不适感。
