# Analysis_Modification_Plan-62: MetaPanel 布局溢出与视觉体验深度优化方案

## § 1 布局溢出与间距异常根因诊断

### 1.1 约束失效导致的横向溢出
- **诊断分析**：当 `MetaPanel` 被放置在 `QScrollArea` 中时，若内部组件（如 `lblPath` 或 `ElasticEdit`）的文本内容过长，布局管理器会倾向于扩张宽度而非折行，导致内容“刺破”面板边界。
- **傻逼之处**：现有的 `resizeEvent` 试图手动设置 `maximumWidth`，但在 Qt 的布局刷新周期中，这种手动干预往往滞后或被布局引擎的 `sizeHint` 逻辑覆盖，导致无法实现真正的“绝对不溢出”。

### 1.2 视觉噪音与垂直间距冗余
- **诊断分析**：
    - **图标干扰**：在“链接”、“标签”、“分类”等小节中使用 SVG 图标占用了宝贵的横向空间，且在窄面板模式下显得过于拥挤。
    - **间距累加效应**：全局布局的 `spacing(12)` 与子组件行内部的 `contentsMargins(4)` 产生了叠加效应，导致行与行之间的空隙高达 20px，视觉上产生了严重的断裂感。

---

## § 2 工业级视觉与布局优化方案

### 2.1 彻底的宽度防御策略（强制响应式）
- **修改建议**：
    1. **锁定容器宽度**：在 `MetaPanel` 的 `resizeEvent` 中，强制将 `m_container` 的固定宽度设为 `m_scrollArea->viewport()->width()`。
    2. **自动换行/省略逻辑**：
        - 对于 `lblPath`：启用 `setWordWrap(true)` 并结合 `setMaximumWidth`。
        - 对于 `ElasticEdit`：在 `adjustHeight` 时显式指定 `document()->setTextWidth(fixedWidth)`，确保高度计算完全基于锁定的宽度。
    3. **杜绝横向滚动条**：明确设置 `m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)`。

### 2.2 视觉降噪与密度优化（响应用户特定需求）
- **修改建议**：
    1. **移除特定图标**：
        - 移除 `m_linkEdit` 左侧的 `link` 图标。
        - 移除 `tagL` 顶部的 `tag` 图标及标题头。
        - 移除 `catL` 顶部的 `category` 图标及标题头。
        - **优化理由**：通过占位符（PlaceholderText）即可明确输入框意图，移除图标可为文本内容腾出约 24px 的横向空间，并显著提升视觉简洁度。
    2. **压缩垂直间距**：
        - 将全局间距 `m_containerLayout->setSpacing(8)`。
        - 将 `addInfoRow` 内部的垂直边距压至 `2px`。
        - 最终实现行间距约 **12px** 的工业级紧凑视觉效果。

### 2.3 强制执行 10px 边距
- **修改建议**：
    1. 确保 `m_containerLayout` 的 `setContentsMargins(10, 10, 10, 10)` 是唯一的边距来源。
    2. 移除所有子组件包装器（如 `palWrapper`）可能带有的额外边距。

---

## § 3 总结
通过“锁定容器宽”+“移除视觉冗余”+“压缩间距”的三位一体方案，可以将 `MetaPanel` 从一个松散、易溢出的原型，提升为具备严谨工业美感的专业元数据面板。
