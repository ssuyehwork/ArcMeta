## 分析计划 #30 ｜ [2026-05-27 10:15:00]

### § 0 需求原文（逐字引用用户描述，禁止意译）
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

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| ContentPanel.cpp | `src/ui/ContentPanel.cpp` | 内容面板，包含虚拟模型实现 | ~1100 行 |

**1.2 现有架构问题定位**
- 问题一：`FerrexVirtualDbModel::data` 处理 `Qt::DisplayRole` 时，对 `case 0`（名称列）直接调用 `QFileInfo(path).fileName()`。
  - 根因：对于磁盘根目录（如 `C:/`），`QFileInfo::fileName()` 返回空字符串，导致“此电脑”视图下盘符标签丢失。
  - 影响面：所有显示磁盘根目录的视图（此电脑、导航树同步等）。

**1.3 调用链 / 数据流图**
```
View (GridView/TreeView) 
  └─► FerrexVirtualDbModel::data(index, Qt::DisplayRole)
        └─► QFileInfo(path).fileName()  <-- 当 path 为 "C:/" 时返回 ""
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 方案 A (模型层修正) | 逻辑集中，一处修改全局生效，不破坏虚拟模型设计 | 需额外创建 QFileInfo 对象 | ✅ 采用 | — |
| 方案 B (数据源层处理) | 减少 data() 中的计算开销 | 增加 ItemRecord 复杂度，破坏模型职责单一性 | ❌ 放弃 | 风险较高且不够通用 |

**2.2 目标架构设计**
- 设计原则遵循：Robustness (鲁棒性)。
- 核心改动逻辑：在 `data()` 接口中引入 `isRoot()` 物理检查。
- 新数据流：
```
FerrexVirtualDbModel::data
  └─► QFileInfo(path)
        ├─► fileName() 非空 -> 返回 fileName
        └─► fileName() 为空 && isRoot() -> 返回 toNativeSeparators(absoluteFilePath)
```

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | ContentPanel.cpp | `src/ui/ContentPanel.cpp` | 修改 | 重构 `FerrexVirtualDbModel::data` 中名称显示逻辑 | ~8 行 |

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 性能下降 | 列表视图项极多时频繁创建 QFileInfo | 1 | 2 | 2 | 观察滑动流畅度，必要时将 fileName 缓存至 Record |

### § 5 测试策略（Test Strategy）
- 单元测试：验证磁盘根目录路径（"C:/", "D:/"）在模型中返回正确的盘符字符串。
- 回归风险点：确保普通文件夹和文件显示正常，不显示全路径。

### § 8 审批状态

**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
