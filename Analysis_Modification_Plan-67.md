---

## 分析计划 #67 ｜ 2026-06-xx 15:00:00

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 你没有成功修改到痛点，再深度排查是否与mainwindow有直接关系

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 元数据面板 UI 实现 | ~650 行 |
| MainWindow.cpp | `src/ui/MainWindow.cpp` | 主窗口 UI 实现与面板管理 | ~1100 行 |

**1.2 现有架构问题定位**
- 问题一：宽度获取回退逻辑错误。
  - 根因：在 `MetaPanel::resizeEvent` 中，原本存在一个容错逻辑：当 `viewportW < 50` 时，尝试使用 `parentWidget()->width()`。然而，`MetaPanel` 的父组件是 `MainWindow`（或其内部的 Splitter），其宽度远大于 `MetaPanel` 本身。这会导致 `MetaPanel` 误以为自己有上千像素的宽度，从而按照错误的超大宽度计算子控件高度，导致内容截断或显示不全。
- 问题二：布局同步的时序性冲突。
  - 根因：虽然引入了延迟刷新，但由于宽度锁定（`setFixedWidth`）的基准值错误（基于了父窗口宽度），导致后续的 `adjustHeight` 计算出的高度远小于实际所需的折行高度。

**1.3 调用链 / 数据流图**
```
MainWindow.resize() -> QSplitter 分配宽度 230px
  └─► MetaPanel.resizeEvent()
        └─► viewportW 此时为 0 (尚未完成 Layout)
              └─► 触发错误回退：viewportW = MainWindow.width() (1200px)
                    └─► m_container->setFixedWidth(1200px)
                          └─► ElasticEdit 按照 1200px 计算高度 (单行高度)
                                └─► 实际上物理宽度只有 230px -> 内容被迫换行但高度不足 -> 截断
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 方案 A：修正回退逻辑 | 确保宽度计算始终以面板自身为基准 | 无 | ✅ 采用 | — |

**2.2 目标架构设计**
- 核心改动逻辑：
  1. 废除 `parentWidget()->width()` 引用，改用 `this->width()` 作为备选。
  2. 强化宽度锁定机制：只有当宽度能够代表面板真实物理尺寸时才进行锁定。
  3. 保持 `setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere)` 以处理长路径。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 修改 | 修正 `resizeEvent` 宽度回退逻辑 | ~10 行 |

### § 8 审批状态

✅ 2026-06-xx 已完成修复并更新文档。
