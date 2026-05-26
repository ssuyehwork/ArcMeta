## 分析计划 #25 ｜ [2026-05-26 13:58:15]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 严格按照下面提示要求去修改，避免脑补又再次被破坏
>
> # 代码分析与修改方案 - 修复“此电脑”盘符显示问题
>
> ## 1. 问题背景
> 在当前版本的“此电脑”界面中，内容容器（ContentPanel）内的硬盘图标下方未能正确显示盘符（如 C:/, D:/ 等）。这导致用户无法识别具体的磁盘分区。该问题源于 Ai 在将模型重构为虚拟数据库模型（`FerrexVirtualDbModel`）时，对文件名称的提取逻辑处理不当。
>
> ## 2. 逻辑架构分析与对比
>
> ### 当前版本 (src/ui/ContentPanel.cpp)
> 当前版本采用了 `FerrexVirtualDbModel`。在 `data()` 函数中，对于显示名称（`Qt::DisplayRole`）的处理如下：
> ```cpp
> if (role == Qt::DisplayRole || role == Qt::EditRole) {
>     switch (index.column()) {
>         case 0: return QFileInfo(path).fileName();
>         // ...
>     }
> }
> ```
> **故障原因**：当路径为磁盘根目录（例如 `C:/`）时，`QFileInfo::fileName()` 会返回一个**空字符串**。由于虚拟模型统一使用该函数获取显示名称，导致硬盘项的标签为空白。
>
> ### 旧版本-3 (旧版本-3/src/ui/ContentPanel.cpp)
> 旧版本使用的是 `QStandardItemModel`，在加载“此电脑”数据时直接指定了显示文本：
> ```cpp
> for (const QFileInfo& drive : drives) {
>     QString drivePath = drive.absolutePath();
>     // ...
>     QIcon driveIcon = provider.icon(drive);
>     auto* item = new QStandardItem(driveIcon, drivePath); // 盘符直接作为名称传入
>     // ...
> }
> ```
> 旧版本通过在创建条目时直接绑定 `drivePath`（如 "C:/"），规避了后续解析的问题。
>
> ## 3. 详细修复方案
>
> 为了在保持虚拟模型高性能的同时修复此显示 Bug，建议对 `src/ui/ContentPanel.cpp` 中的数据提供逻辑进行物理加固。
>
> ### 修改目标文件：`src/ui/ContentPanel.cpp`
>
> #### 方案 A：在模型层修正（推荐，最彻底）
> 修改 `FerrexVirtualDbModel::data` 函数中对 `Qt::DisplayRole` 的处理逻辑，增加对根目录的特殊识别。
>
> **修改建议：**
> 找到 `FerrexVirtualDbModel::data` 函数（约第 106 行），将：
> ```cpp
> case 0: return QFileInfo(path).fileName();
> ```
> 替换为以下健壮性逻辑：
> ```cpp
> case 0: {
>     QFileInfo info(path);
>     QString name = info.fileName();
>     // 如果文件名为空且为根目录（磁盘），则返回完整路径作为显示名
>     if (name.isEmpty() && info.isRoot()) {
>         return QDir::toNativeSeparators(info.absoluteFilePath());
>     }
>     return name;
> }
> ```
>
> #### 方案 B：在数据源层处理（可选）
> 在 `loadDirectory` 函数处理 `computer://` 逻辑时，利用 `ItemRecord` 结构中的 `categoryName` 字段（或新增字段）来预存显示名称。但方案 A 更加通用，且不改变数据结构，风险最低。
>
> ## 4. 验证点
> 1. **盘符显示**：点击“此电脑”后，硬盘图标下方应显示 `C:\`, `D:\` 等字样。
> 2. **列表视图同步**：切换到列表模式（ListView），“名称”列也应正确显示盘符。
> 3. **正常文件不受影响**：进入普通文件夹后，普通文件和文件夹仍应正常显示其短文件名，而非全路径。
>
> ## 5. 结论
> 通过在虚拟模型的 `data()` 接口中引入对 `QFileInfo::isRoot()` 的判断，可以完美恢复因重构丢失的盘符显示功能，确保 UI 逻辑与旧版本 3 的行为保持一致。

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.cpp | src/ui/ContentPanel.cpp | 内容面板，实现虚拟模型 `FerrexVirtualDbModel` | 约 1100 行 |

**1.2 现有架构问题定位**
- 问题一：`FerrexVirtualDbModel::data` (117行) — 盘符显示为空
  - 根因：对于根目录（如 `C:/`），`QFileInfo::fileName()` 返回空字符串。
  - 影响面：所有显示磁盘盘符的地方，包括网格视图和列表视图。

**1.3 调用链 / 数据流图**
```
ContentPanel::loadDirectory("computer://")
  └─► QDir::drives()
  └─► m_model->setRecords(driveRecords)
        └─► FerrexVirtualDbModel::data(index, Qt::DisplayRole)
              └─► QFileInfo(path).fileName()  <-- 返回空字符串 (Bug 点)
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 方案 A (模型层修正) | 逻辑集中，一处修改全局生效，不破坏数据结构 | 无 | ✅ 采用 | — |
| 方案 B (数据源层预处理) | 逻辑清晰 | 需要修改数据结构或滥用现有字段，耦合度高 | ❌ 放弃 | 方案 A 更优雅 |

**2.2 目标架构设计**
- 设计原则遵循：健壮性原则。
- 核心改动逻辑：在获取文件名前，先通过 `QFileInfo::isRoot()` 判定。如果是根目录且文件名为空，则回退到 `absoluteFilePath()`。
- 新调用链 / 数据流：
```
FerrexVirtualDbModel::data(index, Qt::DisplayRole)
  └─► QFileInfo info(path)
  └─► if (info.fileName().isEmpty() && info.isRoot())
        return QDir::toNativeSeparators(info.absoluteFilePath())
      else
        return info.fileName()
```

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | ContentPanel.cpp | src/ui/ContentPanel.cpp | 修改 | 增强 `FerrexVirtualDbModel::data` 中对盘符名称的处理 | ~8 行 |

**2.4 性能影响评估**
- 时间复杂度变化：无显著变化，增加一个简单的布尔判断。
- 空间复杂度变化：无变化。
- 预期响应时间影响：忽略不计。
- 数据库查询次数变化：无。

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 误伤普通文件 | 无 | 2 | 1 | 2 | 仅在 `fileName()` 为空且 `isRoot()` 为真时生效 |

### § 4 依赖与兼容性检查

- 新增外部依赖：无
- 破坏性变更：无
- 受影响的其他模块：无
- 向下兼容策略：完全兼容

### § 5 测试策略（Test Strategy）

- 单元测试：无
- 集成测试：人工点击“此电脑”验证盘符显示。
- 回归风险点：普通文件夹内的文件名称显示。

### § 6 回滚方案（Rollback Plan）

- 回滚触发条件：文件显示异常。
- 回滚步骤：
  1. 还原 `src/ui/ContentPanel.cpp`。
- 回滚预计耗时：1 分钟

### § 7 执行前置条件

- [x] 用户已审阅并批准本分析计划

### § 8 审批状态

**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**

✅ [2026-05-26 14:00:00] 已获授权，开始执行

### § 9 执行结果（事后填写）

**实际完成时间：** [2026-05-26 14:04:15]
**对应 Modification_Record.md 变更序号：** #[4]
**计划偏差说明：**
  - 原计划 vs 实际执行差异：与计划一致
  - 超出计划范围的操作：无
**最终状态：** ✅ 完成
