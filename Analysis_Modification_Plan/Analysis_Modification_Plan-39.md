# Analysis and Modification Plan - 筛选面板间距标准化

## 1. 现状分析 (Current State)
经排查 `src/ui/FilterPanel.cpp`，目前的水平间距呈现碎片化状态，不符合美观统一的原则：
- **顶部标题栏**：左边距 15px，右边距 5px (注：按照要求，此部分保持现状)。
- **复选框行 (ClickableRow)**：左右边距均为 4px。
- **分组折叠标题 (QPushButton)**：左内边距 (padding-left) 为 8px。
- **快速输入框 (QLineEdit)**：左右外边距 (margin) 为 8px。
- **颜色矩阵容器 (FlowLayout)**：左边距 8px。
- **文件大小输入布局**：左右边距 8px。

## 2. 目标方案 (Proposed Solution)
除顶部标题栏外，将筛选面板内所有组件的水平间距（左、右）统一强制设定为 **5 像素 (5px)**，形成垂直对齐。

### 2.1 复选框行调整
修改以下位置的 `ClickableRow` 布局边距：
1. **通用行**：在 `addFilterRow` 函数中，将 `rl->setContentsMargins(4, 0, 4, 0);` 修改为 `(5, 0, 5, 0)`。
2. **链接 (Links)**：在 `rebuildGroups` 的链接处理块中，将 `rl->setContentsMargins(4, 0, 4, 0);` 修改为 `(5, 0, 5, 0)`。
3. **备注 (Notes)**：在 `rebuildGroups` 的备注处理块中，将 `rl->setContentsMargins(4, 0, 4, 0);` 修改为 `(5, 0, 5, 0)`。
4. **图像比例 (Aspect Ratio)**：在 `rebuildGroups` 的图像比例处理块中，将 `rl->setContentsMargins(4, 0, 4, 0);` 修改为 `(5, 0, 5, 0)`。

### 2.2 分组折叠标题调整
修改 `buildGroup` 函数中 `hdr` 的样式表：
- 将 `padding-left: 8px;` 修改为 `padding-left: 5px;`。
- 显式添加 `padding-right: 5px;`。

### 2.3 输入框调整 (QLineEdit)
修改 `rebuildGroups` 中所有 `FilterSearchEdit` 的样式表：
- 将 `margin: 4px 8px;` 统一修改为 `margin: 4px 5px;`。
- 涉及变量：`m_editColor`, `m_editTag`, `m_editType`, `m_editCreateDate`, `m_editModifyDate`。

### 2.4 颜色区域调整
在 `rebuildGroups` 的颜色标记逻辑中：
- **色相滑块容器**：`hueLayout->setContentsMargins(4, 0, 4, 0);` 调整为 `(5, 0, 5, 0)`。
- **辅助标签** (标准色系/最近筛选)：`margin-left: 8px;` 调整为 `margin-left: 5px;`。
- **色块矩阵容器**：`setContentsMargins(8, 0, 0, 0);` 调整为 `(5, 0, 5, 0)`。

### 2.5 文件大小布局调整
修改 `rebuildGroups` 中的文件大小布局：
- 将 `hs->setContentsMargins(8, 4, 8, 8);` 调整为 `hs->setContentsMargins(5, 4, 5, 8);`。

## 3. 执行规范与警示 (Strict Directives)
- **严禁脑补与顺手修改**：修改过程中必须严格遵守上述参数，禁止擅自修改其他无关逻辑、样式或“顺手”清理代码。
- **申报备案机制**：如果在修改代码过程中发现任何潜在的 Bug、逻辑缺陷或优化点，**严禁当场修复**。必须第一时间将发现的问题申报登记到根目录下的 `Declaration_Log.md` 文档中进行备案，留待后期统一排查修复。
- **一致性**：最终目标是实现内容在垂直方向上整齐对齐 5px 参考线。
