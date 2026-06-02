# 分析计划 #72 ｜ [2024-06-18 10:45:00]

### § 0 需求原文
> 我要的是可持久化缓存，因为有的文件夹经常需要递归，如果不进行缓存，那么每次都必须实时扫描，这样只会浪费时间

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| `ContentPanel.cpp` | `src/ui/ContentPanel.cpp` | 视图容器，控制扫描逻辑。 | ~1200 |
| `ItemRepo.cpp` | `src/db/ItemRepo.cpp` | 数据库条目操作层。 | ~500 |
| `Database.cpp` | `src/db/Database.cpp` | 数据库初始化与 Schema 定义。 | ~200 |
| `FolderRepo.cpp` | `src/db/FolderRepo.cpp` | 文件夹属性操作层。 | ~100 |

**1.2 现有架构问题定位**
- **问题一：递归扫描不落库** — `ContentPanel::loadDirectory` 中的 `scanDir` 递归结果仅存在内存 `allItems` 向量中。
  - 根因：设计初衷为物理实时同步，未考虑大型目录的 I/O 开销。
  - 影响面：每次点击递归按钮都会造成长达数秒的磁盘 I/O 阻塞（尤其在 HDD 或网络磁盘上）。

- **问题二：缺乏有效的一致性校验机制** — 无法判断数据库中的“旧数据”是否与当前磁盘目录树一致。
  - 根因：没有记录文件夹“上一次完整扫描时间”。

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 纯物理实时扫描 | 数据绝对准确 | 性能差，浪费 I/O | ❌ 放弃 | 无法满足大型目录性能需求 |
| 外部快照文件 (.json) | 不依赖数据库 | 产生大量碎文件，检索慢 | ❌ 放弃 | 难以维护大规模索引 |
| **SQLite 镜像索引** | **检索极快，支持 SQL 复合筛选** | **需要处理同步一致性** | ✅ 采用 | 工业级标准方案 |

**2.2 目标架构设计**
- **设计原则**：基于“根目录修改时间（mtime）”的快照对账机制。
- **核心逻辑**：
  1. 请求递归路径 `P`。
  2. 获取磁盘 `P` 的物理 `mtime_phys`。
  3. 查询数据库 `folders` 表中 `P` 的 `last_recursive_scan_mtime`。
  4. **命中判定**：若 `mtime_phys == last_recursive_scan_mtime`，则视为缓存有效，执行 `SELECT FROM items WHERE path LIKE 'P/%'`。
  5. **失弹判定**：若不一致，执行物理扫描，完成后更新数据库并将新 `mtime_phys` 存入 `folders` 表。

**2.3 逐文件改动计划**

| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 |
|---|--------|----------|----------|--------------|
| 1 | `Database.cpp` | `src/db/Database.cpp` | 修改 | `folders` 表新增 `last_recursive_scan_mtime` (BIGINT)；为 `items(path)` 建立索引。 |
| 2 | `ItemRepo.h/cpp` | `src/db/ItemRepo.cpp` | 新增 | 实现 `getRecursiveItemsFromDb` 与 `saveBatch` 接口。 |
| 3 | `FolderRepo.h/cpp` | `src/db/FolderRepo.cpp` | 新增 | 实现 `updateScanTime` 与 `getScanTime`。 |
| 4 | `ContentPanel.cpp` | `src/ui/ContentPanel.cpp` | 修改 | 重构 `loadDirectory`：引入缓存校验分流逻辑。 |

**2.4 性能影响评估**
- 时间复杂度：O(Disk_Traverse) → O(Log_N) (索引检索)。
- 响应速度：10,000+ 文件从 ~2000ms 降至 ~30ms。

### § 3 风险矩阵

| 风险项 | 触发条件 | 严重程度 | 概率 | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 深度子目录变更感知失效 | 修改了子目录但父目录 mtime 未变 | 3 | 4 | 12 | 提供手动“强制刷新”操作，或引入深度 checksum 校验（开销较大需权衡）。 |

### § 5 测试策略
- 在 `D:/TestLargeFolder` (5万文件) 测试首次加载时间。
- 在 `D:/TestLargeFolder` 测试二次（命中缓存）加载时间。
- 修改任意文件名，测试缓存失效并自动重扫逻辑。

---

**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
