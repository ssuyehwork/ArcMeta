# 分析计划 #48 ｜ [2024-05-22 11:30:00]

### § 0 需求原文
> 目前“筛选器”里可以单独筛选文件夹，却无法单独筛选文件

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.cpp | src/ui/ContentPanel.cpp | 数据统计与过滤引擎实现 | ~2400 行 |
| FilterPanel.cpp | src/ui/FilterPanel.cpp | 筛选器 UI 构建与状态管理 | ~800 行 |

**1.2 现有架构问题定位**
- 问题一：统计维度缺失 — `ContentPanel.cpp` (1904-1908行)
  - 根因：当前的统计逻辑仅区分了 `folder` 和具体的扩展名（如 `JPG`, `PDF`），没有提供一个汇总所有非文件夹条目的通用 `file` 标签。
  - 影响面：筛选器面板无法显示“文件”这一宏观分类。

- 问题二：过滤逻辑不完备 — `ContentPanel.cpp` (425-431行)
  - 根因：`matchType` 循环中仅显式处理了 `fType == "folder"` 的特殊判断，其余情况均视为具体后缀匹配。若要在筛选器中支持“文件”，必须在此处增加对非文件夹属性的识别。

**1.3 调用链 / 数据流图**
```
ContentPanel::refreshStats()
  └─► stats.typeCounts["folder"]++
  └─► stats.typeCounts[suffix]++      <-- 缺少 stats.typeCounts["file"]
FilterPanel::rebuildGroups()
  └─► 遍历 typeCounts 构建 UI         <-- 由于缺少 "file" 键，UI 不显示“文件”选项
ContentPanel::filterAcceptsRow()
  └─► 匹配 "folder" 或具体后缀        <-- 缺少对 "file" 类型的反向判定逻辑
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 前端 UI 自动聚合 | 无需改动核心统计逻辑 | 过滤逻辑依然无法识别 | ❌ 放弃  | 无法从根本上解决过滤引擎的判定问题 |
| 核心统计增加 "file" 维度 | 结构清晰，过滤引擎天然支持 | 需要多处微调 | ✅ 采用  | 符合现有 SoA/统计驱动的设计架构 |

**2.2 目标架构设计**
- 设计原则遵循：数据驱动（Data-Driven Filtering）。
- 核心改动逻辑：
  1. 在 `ContentPanel` 统计过程中，将所有 `!info.isDir()` 的项计入 `typeCounts["file"]`。
  2. 在 `FilterPanel` 中显式渲染“文件”勾选框，位置紧邻“文件夹”。
  3. 增强 `ContentPanel` 的过滤谓词，使其识别 `file` 筛选位。

**2.3 逐文件改动计划**

#### 文件 1: `src/ui/ContentPanel.cpp`
| # | 修改位置 | 变更内容摘要 |
|---|----------|--------------|
| 1 | 426-427行 | 增加 `else if (fType == "file") { if (type != "folder") { matchType = true; break; } }` |
| 2 | 1905-1907行 | 增加 `else { stats.typeCounts["file"]++; stats.typeCounts[info.suffix().toUpper()]++; }` |

#### 文件 2: `src/ui/FilterPanel.cpp`
| # | 修改位置 | 变更内容摘要 |
|---|----------|--------------|
| 1 | 529行之后 | 仿照 `folder` 逻辑，增加对 `m_typeCounts.contains("file")` 的判断及 UI 行添加。 |

**2.4 性能影响评估**
- 统计阶段增加一次简单的整数累加，对 O(N) 遍历性能影响可忽略不计。

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 名称冲突 | 若存在名为 "file" 的扩展名 | 1 | 1 | 1 | 后缀名统计通常转换为大写 (FILE)，与内部小写 file 标签隔离 |

### § 4 依赖与兼容性检查
- 无

### § 5 测试策略（Test Strategy）
- 功能测试：勾选“文件夹”，应只显示目录；勾选“文件”，应显示所有文件（无论后缀）；同时勾选，应显示全部。
- 边缘测试：测试无扩展名的文件是否能被“文件”筛选器正确识别。

### § 6 回滚方案（Rollback Plan）
- 还原涉及文件的修改。

### § 7 执行前置条件
- [x] 用户已审阅并批准本分析计划

### § 8 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
