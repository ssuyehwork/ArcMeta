# Analysis & Modification Plan - 19: ContentPanel 名称列物理锁定深度修复方案 (最终确认版)

## 1. 根本原因排查与架构分析 (Diagnostic)
启动或缩放时，`ContentPanel` 名称列（索引 0）宽度异常收缩的三个深层原因：
- **Splitter 空间挤压**：`MainWindow` 赋予侧边栏过多硬性空间 (920px)，导致内容区在默认 1200px 窗口下仅剩 280px。
- **交互列总和过载**：内容区的其它交互列初始总宽约 570px，远超视口承载力。
- **Stretch 模式牺牲机制**：名称列作为 `Stretch` 列，在空间紧缺且禁用水平滚动条时，被 Qt 的 `QHeaderView` 布局引擎优先牺牲。

## 2. 深度解决方案 (Objectives)
- **解压全局布局**：将侧边栏最小宽度调低 (230 -> 200)，MainWindow 初始尺寸提升 (1200 -> 1440)，为内容区争取到约 640px 的初始空间。
- **物理控制权升级**：废除 `QHeaderView::Stretch` 模式，改为 `Interactive` 手动控制。
- **多级锁定与伪拉伸**：
  - 建立优先级回退压缩机制（日期 > 大小 > 类型 > 标签 > 状态），保护 220px 红线。
  - 在空间富余时，通过手动计算实现名称列的“伪拉伸”视觉效果。

## 3. 已实施修改 (Implemented Changes)

### A. MainWindow 布局解压
- **代码文件**: `src/ui/MainWindow.cpp`
- **操作**:
  - `resize(1440, 900);`
  - `setMinimumSize(1280, 720);`
  - 侧边栏 `setMinimumWidth` 统一由 230 改为 200。
  - ContentPanel `setMinimumWidth` 设为 450。

### B. ContentPanel 逻辑重构
- **代码文件**: `src/ui/ContentPanel.cpp`
- **操作**:
  - `initListView`: 将索引 0 设为 `Interactive`。
  - `enforceNameColumnMinimum`: 实现优先级压缩逻辑与伪拉伸算法。
  - `sectionResized`: 增强红线拦截逻辑，确保手动拖拽不侵蚀红线。

## 4. 预期效果 (Expected Outcome)
- 启动界面中，名称列物理锁定为 220 像素以上。
- 缩放视口时，其他列会自动腾挪空间。
- 当视口极度缩小时，宁可出现水平滚动条，也要保住 220px 的名称显示区。
