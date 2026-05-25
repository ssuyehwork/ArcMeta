2026-06-18: 物理锁定 ContentPanel 名称列最小宽度为 220 像素。
- 根本原因：MainWindow 侧边栏占据了过多空间 (920px)，导致内容区在 1200px 窗口下仅剩 280px，无法承载 570px 的交互列。
- 解决策略：废除名称列的 QHeaderView::Stretch 模式，改用 Interactive 配合 setMinimumSectionSize(220) 强行“顶开”布局。
- 铁律：名称列红线优先级高于“无滚动条”视觉要求。
