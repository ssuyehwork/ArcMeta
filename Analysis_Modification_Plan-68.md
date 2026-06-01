## 分析计划 #68 ｜ [2025-05-22 11:00:00]

### § 0 需求原文
> 为什么这个编辑框没有弹性伸缩显示内容呢？我说的是元数据面板里的所有编辑框，为什么你没有严格遵循我的指令去修改？

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 编辑框高度自适应逻辑实现 | ~450 |

**1.2 现有架构问题定位**
- 问题一：`ElasticEdit::adjustHeight()` 依赖的 `document()->size().height()` 在某些布局更新阶段可能返回旧值，导致折行后高度未及时增加。
- 问题二：`MetaPanel::adjustFlowHeights()` 仅处理了 `m_paletteBox`，未处理 `m_tagContainer`（标签容器），导致标签增多换行时容器高度塌陷或截断。
- 问题三：`MetaPanel::resizeEvent` 中的 `singleShot` 延迟处理虽然规避了初始化时的宽度抖动，但若执行时宽度计算仍有微小偏差，会导致子组件未能触发正确的 `adjustHeight`。

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
采用 `document()->documentLayout()->documentSize().height()` 获取更精确的即时高度，并显式考虑 `QPlainTextEdit` 的视口边距（Contents Margins）。

**2.2 核心改动逻辑**
- 优化 `ElasticEdit::adjustHeight`：增加对文档布局高度的强制重算。
- 扩展 `MetaPanel::adjustFlowHeights`：同步计算标签流式布局的高度。
- 强化数据设置接口：在 `setTags` 等方法末尾强制执行高度自适应调用。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 修改 | 重构 `adjustHeight` 与 `adjustFlowHeights` | ~30 行 |

### § 3 风险矩阵

| 风险项 | 触发条件 | 严重程度 | 概率 | 风险值 | 应对措施 |
|--------|----------|----------|------|--------|----------|
| 无限递归 | `adjustHeight` 触发 `resizeEvent` 再次调用自身 | 4 | 2 | 8 | 增加 `height()` 变化判断，仅在数值变动时调用 `setFixedHeight` |

### § 8 审批状态

⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更
