# 正式版调试日志清理 —— Modification_Plan-44.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
为了将系统重构为高品质、高稳定性的正式版本，需要清理全项目中冗余的纯调试、性能跟踪和进度追踪日志（第 1 类），并将代表失败、异常、降级情况但被误标为 `qDebug` 的日志调用提升为 `qWarning` 或 `qCritical`（第 2 类），同时原样保留真正的错误日志（第 3 类），彻底洗净生产版日志。本方案严格遵循 `Development_Plan.md` 第 3 节。

## 2. 问题定位与排查清单

### 2.1 第一类：纯调试跟踪日志（物理删除）

这些点位纯粹用于开发过程中的步骤提示、性能打印或参数查看，对生产排障无实质价值，且无错误、异常语义。**本案将对以下代码行进行彻底物理删除**：

1. `src/main.cpp`
   - 行 57: `qDebug() << "[Shutdown] >>> 开启 Clean Shutdown 优雅退出流程 <<<";`
   - 行 60: `qDebug() << "[Shutdown] 正在等待子线程池安全退场...";`
   - 行 62: `qDebug() << "[Shutdown] 全局子线程已完全退场";`
   - 行 65: `qDebug() << "[Shutdown] 正在强制元数据及 SQLite 落盘...";`
   - 行 67: `qDebug() << "[Shutdown] 持久化落盘完毕";`
   - 行 75: `qDebug() << "[Shutdown] COM 套间释放完毕";`
   - 行 81: `qDebug() << "[Shutdown] 单实例 Mutex 互斥锁已完全释放";`
   - 行 84: `qDebug() << "[Shutdown] <<< Clean Shutdown 正常退场，系统安全关闭 <<<";`
   - 行 114: `qDebug() << "================ ArcMeta 启动加载 ================";`
   - 行 115: `qDebug() << "[PERF] 程序入口点计时开始";`
   - 行 156: `qDebug() << "[PERF] MainWindow 构造耗时:" << ...;`
   - 行 165: `qDebug() << "[PERF] MainWindow->show() 调用已下发...:" << ...;`
   - 行 175: `qDebug() << "[PERF] main 函数逻辑执行完毕...总耗时:" << ...;`

2. `src/ui/MainWindow.cpp`
   - 行 193: `qDebug() << "[Main] MainWindow 构造开始执行";`
   - 行 283: `qDebug() << "[Main] MainWindow 构造函数 UI/托盘初始化完成";`
   - 行 294: `qDebug() << "[Main] 执行延迟首次加载: 恢复历史状态 ->" << lastPath;`
   - 行 297: `qDebug() << "[Main] 历史路径无效，回退至: 此电脑";`
   - 行 956: `qDebug() << "[Main] showEvent 触发...";`
   - 行 960: `qDebug() << "[Main] 正在排期延迟加载任务...";`
   - 行 963: `qDebug() << "[Main] 延迟加载任务开始执行...";`
   - 行 968: `qDebug() << "[PERF] CategoryPanel 初始化耗时:" << ...;`
   - 行 973: `qDebug() << "[PERF] NavPanel 初始化耗时:" << ...;`
   - 行 978: `qDebug() << "[PERF] ContentPanel 初始化耗时:" << ...;`
   - 行 984: `qDebug() << "[PERF] 所有核心面板数据延迟加载完成...ms";`
   - 行 2014: `qDebug() << "[Main] Drive button clicked (TODO)";`

3. `src/ui/LoadingWindow.cpp`
   - 全文件所有 7 处 `qDebug`（构造、进度、关闭等提示日志，对用户不具备生产排障价值，物理删除）。

4. `src/ui/ContentPanel.cpp`
   - 行 564, 566: `deferredInit` 的开始/结束调试日志。
   - 行 836: `qDebug() << "[GridSize] Zoom:" << m_zoomLevel;`（临时缩放值打印）。
   - 行 1867: `qDebug() << "[Content] 后台物理迁移完成，安全触发 UI 异步无感防闪载入";`（日常步骤提示）。

5. `src/ui/NavPanel.cpp`
   - 全文件所有 5 处 `qDebug`（开始/跳过 deferredInit、磁盘图标填充完成等，物理删除）。

6. `src/meta/MediaExtractorPipeline.cpp`
   - 全文件所有 6 处 `qDebug`（入队/出队大小、异步任务提取批次数量、优雅丢弃提示等，物理删除）。

7. `src/meta/MetadataManager.cpp`
   - 行 371: `qDebug() << "[PERF] SQLite 元数据镜像构建完成...";`
   - 行 594: `qDebug() << "[Metadata] [Plan-131] 执行解析流水线 ->" << ...;`
   - 行 731: `qDebug() << "[DB_TRACE] calculateAndPersistProgress 开始计算导入进度...";`
   - 行 767: `qDebug() << "[DB_TRACE] calculateAndPersistProgress 写入进度数据成功...";`
   - 行 1366: `qDebug() << "[DB_TRACE] setItemVisualMetadata 判定为文件夹，触发 categories 颜色同步...";`
   - 行 2117: `qDebug() << "[Metadata] 已执行永久删除清理，通知 UI 刷新:" << ...;`
   - 行 2391: `qDebug() << "[DB_TRACE] persistAsync 成功锁定驱动盘递归互斥锁...";`
   - 行 2465: `qDebug() << "[DB_TRACE] persistAsync 写入内存库成功...";`

8. `src/meta/CategoryRepo.cpp`
   - 行 266: `qDebug() << "[CategoryRepo] add success across dbs: Name =" << ...;`
   - 行 573: `qDebug() << "[CategoryRepo] findCategoryId found:" << ...;`
   - 行 1193: `qDebug() << "[Recount] CategoryRepo::fullRecount triggered...";`

---

### 2.2 第二类：异常、失败或降级日志（重构提升为 qWarning/qCritical 保留）

这些点位记录了严重的失败或异常行为，但此前被错误标记为 `qDebug`。**本案将其提升等级以在生产环境中提供核心排障信息**：

1. `src/meta/CategoryRepo.cpp`
   - 原行 270 (添加分类时底层 DB 操作失败):
     `qDebug() << "[CategoryRepo] add FAILED during step:" << sqlite3_errmsg(mainDb) << ...;`
     **重构为 qCritical**:
     `qCritical() << "[CategoryRepo] add FAILED during step:" << sqlite3_errmsg(mainDb) << ...;`
   - 原行 274 (添加分类时 prepare SQL 失败):
     `qDebug() << "[CategoryRepo] add FAILED during prepare:" << sqlite3_errmsg(mainDb) << ...;`
     **重构为 qCritical**:
     `qCritical() << "[CategoryRepo] add FAILED during prepare:" << sqlite3_errmsg(mainDb) << ...;`

2. `src/ui/MainWindow.cpp`
   - 原行 2104 (检测到监控目录在物理磁盘上已被用户删除或不存在，需要自动剔除):
     `qDebug() << "[DriveBar] 检测到监控文件夹在硬盘上已失效，自动清退:" << finalPath;`
     **重构为 qWarning**:
     `qWarning() << "[DriveBar] 检测到监控文件夹在硬盘上已失效，自动清退:" << finalPath;`

3. `src/meta/TagRepository.cpp`
   - 原行 222 (检测到某个驱动盘存在未迁移迁移的数据，自愈纠偏):
     `qDebug() << "[TagRepository] Detected unmigrated tag data in drive" << ... << ". Migrating...";`
     **重构为 qWarning**:
     `qWarning() << "[TagRepository] Detected unmigrated tag data in drive" << ... << ". Migrating...";`

4. `src/util/ShellHelper.cpp`
   - 原行 177 (检测到盘符漂移，对数据库文件夹进行强制纠偏重命名):
     `qDebug() << "[ShellHelper] 检测到盘符漂移，执行物理纠偏重命名:" << ...;`
     **重构为 qWarning**:
     `qWarning() << "[ShellHelper] 检测到盘符漂移，执行物理纠偏重命名:" << ...;`

---

### 2.3 第三类：已经是 qWarning / qCritical 的警报

原样保留，不做任何触碰。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 纯调试性质的日志输出物理删除（对应用户原话：“纯调试跟踪信息...物理删除代码行...不允许保留空的日志调用框架”） | 2.1 节列出的所有对开发性能、流程提示无生产排障价值的 qDebug 行进行彻底物理删除 | ✅ 一致 |
| 2    | 失败、异常、降级情况被误标记为 qDebug 的升格保留（对应用户原话：“内容属于失败/异常/降级但被错误标记为 qDebug 的日志...改为 qWarning 或 qCritical”） | 2.2 节列出的所有严重异常和降级点位物理保留，并重构为对应的 qWarning / qCritical 级别 | ✅ 一致 |
| 3    | 已经是 qWarning / qCritical 的错误告警原样保留（对应用户原话：“已经是 qWarning / qCritical 的错误告警——原样保留，不做任何改动。”） | 2.3 节明确所有原有的错误告警绝不予以改动 | ✅ 一致 |
| 4    | 中性无法确定是否有排障价值的日志禁止自行判断删除（对应用户原话：“内容本身不含'失败'...'错误'...'异常'等明确失败语义、且无法确定是否有排障价值，禁止自行判断删除，必须列入待确认清单”） | 8.0 节待确认事项中列出，等待用户裁决后方可执行 | ✅ 一致 |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

由于文件较多，且需等待用户对中性日志裁决，本方案目前暂不包含最终物理替换块。在收到用户的共识和待确认裁决后，新方案 `Modification_Plan-45.md` 将输出最终的 Git merge diff 修改块。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/main.cpp`
- [ ] `src/ui/MainWindow.cpp`
- [ ] `src/ui/LoadingWindow.cpp`
- [ ] `src/ui/ContentPanel.cpp`
- [ ] `src/ui/NavPanel.cpp`
- [ ] `src/meta/MediaExtractorPipeline.cpp`
- [ ] `src/meta/MetadataManager.cpp`
- [ ] `src/meta/CategoryRepo.cpp`
- [ ] `src/meta/TagRepository.cpp`
- [ ] `src/util/ShellHelper.cpp`

**明确禁止越界修改的范围：**
- [ ] `sqlite3.c` （SQLite 第三方内核代码）—— 不修改
- [ ] `Logger.h` 里的自定义日志落盘类 —— 不修改
- [ ] 原本已经是 `qWarning` / `qCritical` 的代码行 —— 100% 保持不动

## 6. 实现准则与预警【核心】
1. **彻底物理删除**：对于物理删除行，必须连带删除换行符、空的括号以及大括号包裹，不留空行，不采用加注 `//` 注释的方法保留。
2. **零脑补改动**：在裁决之前绝对不自行判断删除中性日志，防止造成关键故障排障依据的丢失。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 托管模式与磁盘模式逻辑运行与数据加载路径不发生任何溢流。 | ✅ 符合（日志清理属于纯粹的代码层净化，完全不破坏双轨制的纯净边界） |

---

## 8. 待确认事项 (请用户在批准 Plan-44 时一并予以裁决)

以下日志虽内容包含部分“迁移”、“自愈”、“提取”、“纠偏”或“缓存”，但不含明显的“失败、错误”字眼，且价值难以评估。**根据规范，禁止自行删除，在此列出请您进行裁决（回复“保留”或“删除”）：**

### 1号待确认日志：`src/meta/TagRepository.cpp` (标签迁移成功提示)
* **代码**: `qDebug() << "[TagRepository] Successfully migrated" << migratingGroups.size() << "tag groups from drive" << QString::fromStdWString(drive) << "to global.db.";`
* **内容**: 驱动器标签成功迁移至全局数据库。
* **建议**: 【保留并提升为 qWarning】，代表数据完整发生了低频的自动升级。

### 2号待确认日志：`src/util/ShellHelper.cpp` (漂移对账物理纠偏、重命名成功)
* **代码**: 
  - `qDebug() << "[ShellHelper] 目标文件已存在，先将其重命名为无效:" << invalidPath;`
  - `qDebug() << "[ShellHelper] 物理重命名成功";`
  - `qDebug() << "[ShellHelper] 自动纠偏：重命名数据库" << bestInfo.fileName() << "->" << expectedFileName;`
  - `qDebug() << "[ShellHelper] 冲突处理：将冗余数据库标记为无效" << ...;`
* **内容**: 盘符漂移时重置/对账数据库的操作过程。
* **建议**: 【保留并全部提升为 qWarning】，该动作低频且极度关键，对盘符漂移故障有极高的排障价值。

### 3号待确认日志：`src/meta/MetadataManager.cpp` (偏移自愈与路径匹配故障修复)
* **代码**:
  - `qDebug() << "[Metadata] 检测到路径偏移，已从内存清理旧条目以防止重复计数:" << ...;`
  - `qDebug() << "[Metadata] 路径匹配失败，已通过 FID 校准原始路径:" << ...;`
  - `qDebug() << "[Metadata] 永久删除项不在数据库中，跳过清理动作:" << ...;`
* **内容**: 路径偏移、路径匹配失败及永久删除找不到的清理。
* **建议**: 【保留并全部提升为 qWarning】。这些虽然代表自愈成功，但属于低频降级和容错处理，保留对文件系统一致性分析极具排障价值。

### 4号待确认日志：`src/meta/CategoryRepo.cpp` (自愈补全根分类提示)
* **代码**: `qDebug() << "[CategoryRepo] 自动修复补全托管根分类:" << QString::fromStdWString(libName);`
* **内容**: 启动时自愈缺失的托管库根分类。
* **建议**: 【保留并提升为 qWarning】，代表发生了自愈数据库初始化。

### 5号待确认日志：`src/ui/MediaColorExtractor.cpp` 及 `src/ui/MainWindow.cpp` (方案提取和托盘退出提示)
* **代码**:
  - `src/ui/MediaColorExtractor.cpp` 中如 `qDebug() << "[MediaColorExtractor][AI/PDF] 方案 B：Windows 原生系统 PDF 引擎矢量渲染成功...` 等 8 个方案提取提示。
  - `src/ui/TrayController.cpp` 中 `qDebug() << "[Exit] 增量数据同步完成，物理占用已释放。程序实现秒退出。`。
* **内容**: 多媒体矢量图片（PDF/AI/EPS/XMP）通过某种方案提取成功的信息，以及托盘秒退成功的提示。
* **建议**: 【物理删除】，属于高频发生的流程成功。
