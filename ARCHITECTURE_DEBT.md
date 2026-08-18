# ArcMeta 代码库全量架构负债与“打补丁”乱象排查清单

> **审查说明**：针对项目 `src/` 目录下全部 256 个 C++ 源码文件（`.cpp`/`.h`）进行了覆盖率 100% 的深度架构审查。本清单逐一列出了所有违背单一职责原则（SRP）、违反 MVC 分层、存在退避打补丁（Fallback Patchwork）以及重复造轮子/僵尸重叠代码的文件点与彻底根治方案。

---

## 1. 核心分类一：UI 层直接越权硬写原生 SQL（严重违背 MVC & SRP）

### 1.1 `src/ui/CategoryPanel.cpp` (Line 952-960 & Line 1023-1031)
- **脏逻辑现状**：`updateTrashCategoryState()` 函数中，UI 面板直接遍历 `DatabaseManager::getActiveMemoryDbs()`，手写 `sqlite3_prepare_v2` / `sqlite3_step` 拼 SQL 查询 `trash_items` 表。
- **违背原则**：MVC 架构严重倒置。UI 面板应该只管界面事件与渲染，绝对不应出现任何 `sqlite3_*` 原生语句。
- **彻底根除方案**：在 `src/meta/` 下创建专职仓储类 `TrashRepository`，收拢 `hasTrashItems()` 查询 API；UI 层仅需一行 `TrashRepository::instance().hasTrashItems()` 调用，零 SQL 污染。（详见 `/specs/UiSqlDecoupling.md`）。

### 1.2 `src/ui/ContentPanel.cpp` (Line 2226-2242)
- **脏逻辑现状**：在撤销恢复逻辑中，主内容面板针对 `DiskNav` 视图，直接调用 `DatabaseManager::getDbForPath()`，手写 `sqlite3_prepare_v2` 执行 `SELECT id, trash_path FROM disk_trash WHERE original_path = ?`。
- **违背原则**：UI 面板越权访问底层 SQLite 数据句柄。
- **彻底根除方案**：将 `disk_trash` 查询收拢至 `TrashRepository::getDiskTrashRecordByPath()`，`ContentPanel` 彻底剥离原生 SQL 代码（详见 `/specs/UiSqlDecoupling.md`）。

---

## 2. 核心分类二：核心数据管家“大杂烩”与退避补丁（违背 SRP & 双重记账）

### 2.1 `src/meta/MetadataManager.cpp` & `MetadataManager.h`
- **脏逻辑现状**：
  1. **职责大杂烩**：`MetadataManager` 作为元数据镜像，内部既管 256 分片内存缓存，又管盘符监视、文件 ID 算法推导、同步/异步 SQLite 落盘、FTS 索引触发，甚至包含 Win32 原生 API 失败时的自愈退避逻辑。
  2. **Fallback ID 补丁（Line 148, Line 2336）**：`generateFallbackFolderId` 当 Base36 算法无法生成 ID 时，退避使用物理 FRN 生成 Fallback ID，在内存和 SQLite 中引发双重 ID 记账风险。
- **违背原则**：单一职责原则（SRP），类体过大且包含降级退避打补丁逻辑。
- **彻底根除方案**：
  1. 将持久化 SQL 读写彻底下沉并收拢至 `MetadataDao`，`MetadataManager` 专职处理内存分片；
  2. 将 128-bit File ID 生成逻辑独立封装为 `FolderIdGenerator` 工具类，统一计算规则，彻底废除 `generateFallbackFolderId` 退避逻辑。

### 2.2 `src/meta/TagRepository.cpp` (Line 196-202)
- **脏逻辑现状**：`checkAndMigrate()` 中先调用 `MftReader::instance().getDriveList()` 查询系统盘符；当返回为空时，写了 `if (drives.empty())` 显式退避调用 `QDir::drives()` 打补丁保底。
- **违背原则**：典型的退避打补丁逻辑，依赖已废弃的 `MftReader` 导致每次都走退避保底代码。
- **彻底根除方案**：彻底移除对 `MftReader.h` 的依赖，统一使用 Qt 标准跨平台接口 `QDir::drives()`（详见 `/specs/MftDecoupling.md`）。

---

## 3. 核心分类三：底层组件职责杂糅与僵尸代码（历史残留负债）

### 3.1 `src/mft/MftReader.cpp` & `MftReader.h`
- **脏逻辑现状**：在全盘 USN 变更日志与物理扫描弃用后，`MftReader` 内部硬塞了 **UI 图标缓存（`getCachedIcon`）**、**全盘盘符列表（`getDriveList`）**，且单例初始化时强行向 Win32 申请系统级 `SE_BACKUP_NAME` 和 `SE_RESTORE_NAME` 提权。
- **违背原则**：严重违背单一职责原则，且存在无谓的系统提权开销和退场冗余空跑（`TrayController.cpp:73` 中的 `clear()`）。
- **彻底根除方案**：
  1. 抽离 `getCachedIcon` 到专职工具类 `IconCacheManager`；
  2. 彻底物理删除 `MftReader.h` 与 `MftReader.cpp`，更新 CMake 构建清单（详见 `/specs/MftDecoupling.md`）。

### 3.2 `src/util/ShellHelper.cpp` & `ShellHelper.h`
- **脏逻辑现状**：`ShellHelper` 本应专职处理系统 Shell/Explorer 操作（如在资源管理器中打开），但内部强行集成了移动硬盘数据库重命名纠偏（`// 自动纠偏：重命名数据库`）、冲突处理（`// 将冗余数据库标记为无效`）、盘符漂移路由等 SQLite 数据库运维逻辑。
- **违背原则**：功能杂糅，将文件系统运维与 Shell 交互硬塞在一个工具类中。
- **彻底根除方案**：将数据库重命名纠偏与 `.invalid` 标记逻辑移入 `DatabaseManager` 的 `resolveVolumeDrift()` 机制中，`ShellHelper` 仅保留纯粹的操作系统 Shell 交互。

---

## 4. 核心分类四：重复造轮子与功能重叠的“多胞胎”组件

### 4.1 多套批量重命名服务（代码严重重复）
- **重复源文件**：
  1. `src/ui/DiskBatchRenameService.cpp` （磁盘模式重命名）
  2. `src/ui/MemoryBatchRenameService.cpp` （内存模式重命名）
  3. `src/meta/BatchRenameEngine.cpp` （元数据重命名引擎）
- **脏逻辑现状**：重命名规则解析、正则替换、前缀序号生成逻辑在 UI 服务的两个类和 Meta 引擎中重复写了三遍。
- **彻底根除方案**：废除 `DiskBatchRenameService` 与 `MemoryBatchRenameService`，统一收拢至 `BatchRenameEngine`，UI 弹窗仅调用 `BatchRenameEngine` 的强类型 API。

### 4.2 多套媒体属性/特征提取器（逻辑高度散落）
- **重复源文件**：
  1. `src/meta/MediaExtractorPipeline.cpp`
  2. `src/meta/CapsuleMediaExtractor.cpp`
  3. `src/meta/PhysicalDataExtractor.cpp`
  4. `src/util/DiskMediaExtractor.cpp`
- **脏逻辑现状**：提取图像/视频/SVG 的宽高、主色调、EXIF 信息时，在 4 个文件中各自写了一套 API 调用和格式判断逻辑。
- **彻底根除方案**：以 `MediaExtractorPipeline` 为统一唯一的媒体特征提取管道，擦除其余 3 个分散文件的重复代码。

### 4.3 多套物理目录递归扫描器（线程与逻辑分散）
- **重复源文件**：
  1. `src/core/DiskScanService.cpp`
  2. `src/core/PhysicalDiskSearchExtractor.cpp`
  3. `src/core/AutoImportManager.cpp`
- **脏逻辑现状**：文件目录递归、扩展名过滤、排除项判断（如隐藏文件、系统文件）在上述 3 个服务中各自实现了一套，且并发控制各自为政，造成锁竞争。
- **彻底根除方案**：统一收拢至 `DiskScanService` 作为全项目唯一的目录扫描基础设施，其余服务仅作为消费者调度 `DiskScanService`。

---

## 5. 总结与治理路线图

全项目架构负债治理按照 **【解耦剥离 → 集中收拢 → 物理擦除】** 顺序推进：
1. **第一阶段**：彻底根除 UI 层越权 SQL 调用（`UiSqlDecoupling`）；
2. **第二阶段**：彻底物理移除 `MftReader` 并抽离 `IconCacheManager`（`MftDecoupling`）；
3. **第三阶段**：合并重叠的重命名引擎、媒体提取器与目录扫描器，确立单一事实源（SSOT）。
