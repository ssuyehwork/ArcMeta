## 分析计划 #29 ｜ [2026-05-26 15:57:15]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 仍然出现“FILE”，扩大范围排查出根本原因

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.cpp | src/ui/ContentPanel.cpp | 内容面板，驱动数据加载 | 约 1100 行 |
| ItemRepo.cpp | src/db/ItemRepo.cpp | 数据库层，负责分类数据读取 | 约 400 行 |
| ThumbnailDelegate.cpp | src/ui/ThumbnailDelegate.cpp | 委托绘制层 | 约 250 行 |
| JustifiedView.cpp | src/ui/JustifiedView.cpp | 视图布局层 | 约 300 行 |

**1.2 现有架构问题定位**
- **现象描述**：用户在“分类”视图中，虽然应用了筛选，但内容区依然出现带有“FILE”角标的灰色占位块，且没有文件名显示。
- **核心根因（扩大排查结果）**：
  1.  **无效路径入库**：在 `ItemRepo::getRecordsInCategory` 及其相关函数中，通过 `MIN(path)` 获取路径。如果一个文件被重命名或移动，数据库可能残留了旧的 `path`。当进入分类视图时，`m_model->setRecords` 加载了这些已失效的路径。
  2.  **模型层未校验**：`FerrexVirtualDbModel` 拿到路径后，即便文件在物理磁盘上已不存在，也会产生一条 Record。
  3.  **委托层降级渲染**：`ThumbnailDelegate` 绘制时，`QFileInfo(path).exists()` 为假，`QFileInfo(path).fileName()` 返回空，导致出现一个“没名字、没图标、只有类型角标”的诡异灰色块（即用户看到的“FILE”块）。
  4.  **布局与双击逻辑冲突**：`JustifiedView` 的双击判定依赖于 `textRect` 和 `thumbRect`，如果路径失效且无数据，委托绘制的 Rect 依然被 View 计算出来，造成点击无效的死区。

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
在数据源头（数据库查询）进行物理过滤，并在模型加载时进行二次校验。

**2.2 目标架构设计**
1.  **数据库层加固**：修改 `ItemRepo.cpp`，在获取路径列表时，增加 `deleted = 0` 判定（已部分实现），并建议定期清理失效路径（由 SyncEngine 负责，本次重点在展现层屏蔽）。
2.  **展现层容错**：修改 `ContentPanel.cpp` 中的 `loadCategory` 和 `loadPaths`。在将路径存入 `ItemRecord` 之前，强制执行 `QFileInfo::exists()` 物理校验。
3.  **清理残留**：若路径物理不存在，则不加入模型。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 |
|---|--------|----------|----------|--------------|
| 1 | ContentPanel.cpp | src/ui/ContentPanel.cpp | 修改 | `loadCategory` 和 `loadPaths` 增加路径物理存在性校验 |
| 2 | ThumbnailDelegate.cpp | src/ui/ThumbnailDelegate.cpp | 修改 | 在绘制前校验文件名，若为空则视为无效记录，跳过绘制或绘制“已失效”占位 |

### § 3 风险矩阵（Risk Matrix）
- 风险：对于网络路径，`exists()` 可能导致 UI 瞬间卡顿。应对：仅在主线程加载少量记录（分类视图通常记录不多）时同步检查，大量加载时依靠数据库状态位。

### § 7 执行前置条件
- [x] 用户已审阅并批准本分析计划

### § 8 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
