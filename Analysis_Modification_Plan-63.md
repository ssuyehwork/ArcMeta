# 分析计划 #63 ｜ 2026-06-18 16:30:00

### § 0 需求原文
> 1. 扩大范围并且深度排查，为何在启动主程序之后，弹出的mainwindow窗口中的元数据面板为何这样显示（图一）？这肯定存在傻逼的逻辑架构 / 重复定义 / 上下文冲突。当对mainwindow窗口执行最大化然后又恢复窗口时，才会像图二那样正常显示。
> 为什么这些编辑框的逻辑定义不一致吗？为什么它们的宽度都不一致？
> 从现在开始，显示颜色的胶囊状，不再采用胶囊状的方式，而是采用编辑框4像素圆角设计的方式来呈现

### § 1 现状诊断

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.h | src/ui/MetaPanel.h | 元数据面板头文件 | ~220 |
| MetaPanel.cpp | src/ui/MetaPanel.cpp | 元数据面板实现 | ~550 |
| ThumbnailDelegate.cpp | src/ui/ThumbnailDelegate.cpp | 缩略图绘制代理 | ~250 |

**1.2 现有架构问题定位**
- 问题一：启动布局坍塌
  - 根因：Qt 初始显示时 viewport 宽度尚未计算（为0），resizeEvent 同步执行导致子控件宽度被错误锁定。
- 问题二：编辑框逻辑与对齐不一致
  - 根因：混合使用 QLineEdit (标签) 和 ElasticEdit (其他)，且 resizeEvent 中缺少对标签框的强制宽度约束。
- 问题三：视觉风格残留“胶囊状”
  - 根因：旧设计风格残留，部分组件仍使用 drawEllipse 或大半径圆角。

### § 2 方案设计

**2.1 技术选型决策**
采用“全量组件标准化”与“时序延迟刷新”方案。

**2.2 目标架构设计**
- 统一使用 ElasticEdit 承载所有输入。
- 引入 QTimer::singleShot(0) 确保在布局稳定后锁定物理宽度。
- 全系统去胶囊化，推行 4px 圆角矩形标准。

**2.3 逐文件改动计划**
1. MetaPanel.h: 将 m_tagEdit 类型从 QLineEdit 改为 ElasticEdit。
2. MetaPanel.cpp: 
   - 在 resizeEvent 中使用 setFixedWidth 强制对齐所有组件。
   - 重构 PaletteCapsule 和 ColorPickerWidget 的绘制逻辑。
   - 统一编辑框 QSS 样式。
3. ThumbnailDelegate.cpp: 修改星级背景圆角半径为 4px。

### § 9 执行结果

**实际完成时间：** 2026-06-18 17:00:00
**对应 Modification_Record.md 变更序号：** #[38] ~ #[42]
**最终状态：** ✅ 完成
