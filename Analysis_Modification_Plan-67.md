## 分析计划 #67 ｜ [2025-05-22 10:30:00]

### § 0 需求原文
> 1. “MetaPanel”元数据面板里共有多少个编辑框？
> 2. 元数据面板中显示色块的是不是胶囊状？

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.h | `src/ui/MetaPanel.h` | 元数据面板类定义 | ~180 |
| MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 元数据面板逻辑实现 | ~450 |

**1.2 问题定位与识别**

- **编辑框数量识别**：
  在 `MetaPanel::initUi()` 函数中，系统通过 `new ElasticEdit(m_container)` 或类似方式实例化了以下编辑框组件：
  1. `m_nameEdit`: 文件名称编辑框。
  2. `m_noteEdit`: 备注说明编辑框。
  3. `m_linkEdit`: 链接编辑框。
  4. `m_tagEdit`: 标签输入编辑框。
  5. `m_categoryEdit`: 分类展示编辑框（虽然设为只读，但在 UI 架构上属于 `ElasticEdit` 类型组件）。
  结论：共有 **5个** 编辑框。

- **色块形状识别**：
  - 代码层面：`ColorPill` 类在 `src/ui/MetaPanel.cpp` 中定义。其构造函数中调用了 `setFixedSize(16, 16)`，且在 `paintEvent` 中使用 `painter.drawRoundedRect(rect(), 4, 4)` 进行绘制。
  - 视觉层面：16x16 的尺寸配合 4px 的圆角半径，呈现出的是带微圆角的**正方形**，而非长条形的“胶囊状”（Capsule）。
  - 历史背景：代码库中曾经存在过 `PaletteCapsule` 类（根据 `Modification_Record.md` 的描述），但目前的实现已将其重构为流式布局下的 `ColorPill` 方块。

### § 2 方案设计（Solution Architecture）

由于用户仅提出查询问题，暂不涉及代码修改。若需将色块改为胶囊状或调整编辑框数量，需另行设计。

### § 8 审批状态

✅ [2025-05-22 10:35:00] 已完成分析，等待用户确认信息。
