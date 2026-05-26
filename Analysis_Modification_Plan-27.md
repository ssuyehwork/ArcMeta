## 分析计划 #27 ｜ [2026-05-26 14:45:00]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 双击文件夹/文件名称，压根就没有进入到行内编辑状态，你他妈是怎么修改的呢？傻逼Ai

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.h | src/ui/ContentPanel.h | 内容面板头文件，声明虚拟模型 | 约 300 行 |
| ContentPanel.cpp | src/ui/ContentPanel.cpp | 内容面板实现，包含虚拟模型逻辑与网格/列表初始化 | 约 1100 行 |
| ThumbnailDelegate.cpp | src/ui/ThumbnailDelegate.cpp | 缩略图视图（JustifiedView）的委托，处理绘制与编辑 | 约 250 行 |

**1.2 现有架构问题定位**
- 问题一：模型权限缺失（最核心原因）
  - 根因：`FerrexVirtualDbModel` 确实没有重写 `flags()` 函数。在 Qt 架构中，`flags()` 决定了条目的“基因”。如果模型不返回 `Qt::ItemIsEditable`，那么无论通过 F2 快捷键、右键菜单调用 `view->edit()`，还是双击，View 都会在内部直接拦截掉编辑请求，导致“毫无反应”。
- 问题二：视图触发器受限
  - 根因：`ContentPanel::initGridView` 中设置了 `setEditTriggers(QAbstractItemView::EditKeyPressed)`，这导致双击无法触发重命名，仅 F2 键有效。
- 问题三：物理操作未实现
  - 根因：`FerrexVirtualDbModel::setData` 仅发送了信号，并未执行 `QFile::rename`。
- 问题四：委托逻辑覆盖
  - 根因：当 `m_gridView` 使用 `JustifiedView` 时，使用的是 `ThumbnailDelegate`。该委托也需要正确处理 `editorEvent` 或配合模型的 `flags` 才能正常工作。

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
通过“模型权限 + 视图触发器 + 物理执行逻辑”三位一体进行物理加固。

**2.2 目标架构设计**
1.  **模型层**：重写 `flags()`，为第 0 列赋予 `Qt::ItemIsEditable`。
2.  **执行层**：在 `setData()` 中实现物理重命名及元数据、内存缓存的同步更新。
3.  **视图层**：将 `EditTriggers` 放宽，加入 `DoubleClicked` 和 `SelectedClicked`（符合 Windows 习惯）。
4.  **路径兼容**：确保盘符显示修复逻辑在 `data()` 中正确落位。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数 |
|---|--------|----------|----------|--------------|----------------|
| 1 | ContentPanel.h | src/ui/ContentPanel.h | 修改 | 声明 `FerrexVirtualDbModel::flags` | 1 行 |
| 2 | ContentPanel.cpp | src/ui/ContentPanel.cpp | 修改 | 实现 `flags`，补全 `setData` 物理逻辑，放宽视图触发器 | ~30 行 |

### § 3 风险矩阵（Risk Matrix）
- 风险：重命名过程中文件系统冲突。应对：在 `setData` 中增加 `QFile::rename` 成功判定及异常分支处理。

### § 7 执行前置条件
- [x] 用户已审阅并批准本分析计划

### § 8 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**

✅ [2026-05-26 14:47:30] 已获授权，开始执行

### § 9 执行结果（事后填写）

**实际完成时间：** [2026-05-26 14:50:45]
**对应 Modification_Record.md 变更序号：** #[9] ~ #[12]
**计划偏差说明：** 与计划一致
**最终状态：** ✅ 完成
