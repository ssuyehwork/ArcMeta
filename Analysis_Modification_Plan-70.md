# Analysis and Modification Plan - 70: 修复物理路径显示溢出及换行故障

## 1. 问题分析
根据用户提供的最新截图，元数据面板底部的“物理路径”信息虽然开启了 `setWordWrap(true)`，但在面对长路径字符串（尤其是包含下划线、数字等无空格字符的长路径）时，`QLabel` 的原生换行机制失效，导致内容横向溢出容器范围，且未向下撑开高度。
经过代码分析，定位到以下原因：
- **QLabel 换行局限性**：`QLabel` 默认仅在空格或特定标点处换行。对于连续的长路径字符串，它往往会选择截断或溢出。
- **布局高度感知缺失**：在 `addInfoRow` 构造的网格行中，`QLabel` 的高度变化未能有效反馈给父级 `m_container`，导致整行高度固定。
- **视觉风格不统一**：物理路径作为重要信息，目前使用的展示方式无法处理极长路径的阅读需求。

## 2. 修改目标
- **强制换行**：确保物理路径在任何位置（Anywhere）都能根据容器宽度自动换行，不溢出。
- **动态高度**：路径换行后，自动撑开所在行的高度，确保完整显示。
- **工业级交互**：支持鼠标选中路径，方便用户复制。

## 3. 详细修改方案

### 3.1 架构升级：引入路径专用的只读 ElasticEdit
不再使用 `QLabel` 展示物理路径，改为使用已实现的 `ElasticEdit`。
- **优势**：`ElasticEdit` 内部已配置 `WrapAtWordBoundaryOrAnywhere`，能够完美处理路径换行；且自带高度反馈逻辑。
- **定制样式**：去除背景色和边框，使其看起来像普通文本，但保留弹性伸缩能力。

### 3.2 涉及文件改动

#### `src/ui/MetaPanel.h`
- 将 `lblPath` 类型从 `QLabel*` 修改为 `ElasticEdit*`。

#### `src/ui/MetaPanel.cpp`
- **`initUi` 逻辑重构**：针对“物理路径”这一行，不再调用通用的 `addInfoRow`，而是进行特殊布局。
- **样式定制**：为路径控件设置透明背景和无边框样式，字号对齐信息栏。
- **`updateInfo` 同步**：使用 `setPlainText` 更新路径，并触发 `adjustHeight`。
- **`resizeEvent` 同步**：确保路径控件的物理宽度锁定，触发高度重算。

## 4. 调用链 / 数据流图
```
MetaPanel::updateInfo(path)
  └─► m_pathEdit->setPlainText(path)
        └─► ElasticEdit::adjustHeight()
              └─► document()->setTextWidth(fixedW)
              └─► setFixedHeight(calcH)
              └─► m_container->adjustSize()
```

## 5. 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
