# Analysis_Modification_Plan-4: 极简隐藏式“一盘一库” SQLite 架构重构方案

## 1. 架构理解与存储策略
根据用户的最新指令，系统将彻底弃用所有 JSON 相关的逻辑分支。核心目标是实现一种“干净、隐蔽且高效”的元数据存储模型，不考虑任何旧数据兼容逻辑。

### 1.1 存储拓扑
- **根目录托管**：所有数据库文件（`global.db` 及各个卷私有库 `vol_XXXX.db`）均集中存放于主程序所在的根目录下的 `.arcmeta` 文件夹中。
- **物理属性隐藏**：
  - `.arcmeta/` 文件夹本身将被赋予 `FILE_ATTRIBUTE_HIDDEN` 属性。
  - 其中的所有 `.db`、`.db-wal`、`.db-shm` 文件也默认为隐藏状态。

## 2. 详细修改方案

### 2.1 数据库初始化与隐藏属性实现
新建 `src/core/PathManager` (或在 `Database::init` 中扩展)，负责维护存储环境：
- **创建隐藏目录**：
  ```cpp
  std::wstring amDir = qApp->applicationDirPath().toStdWString() + L"\\.arcmeta";
  CreateDirectoryW(amDir.c_str(), NULL);
  SetFileAttributesW(amDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
  ```
- **数据库文件隐藏**：
  在每个 `sqlite3_open` 成功后或 `flushToDisk` 完成后，调用 `SetFileAttributesW` 确保数据库文件及其产生的临时日志文件（WAL）保持隐藏。

### 2.2 彻底移除冗余逻辑 (无保留删除)
- **物理删除相关文件**：彻底移除 `src/meta/AmMetaJson.h` 和 `src/meta/AmMetaJson.cpp`。
- **清理逻辑分支**：
  - 搜索并删除所有 `if (isJsonMode)` 或 `if (m_isJsonMode)` 的代码块。
  - 在 `CategoryRepo` 中删除 `JsonCategoryEngine` 及其相关调用。
  - 在 `MetadataManager` 中删除 `initFromJsonMode`、`initFromDatabase`（需重构为 initFromVolDbs）等函数。
  - 删除所有尝试读取 `arcmeta_categories.json` 或 `.am_meta.json` 的代码逻辑。

### 2.3 “一盘一库”挂载逻辑细节
- **挂载点定位**：
  当硬盘（如 H:\）被激活时，通过卷序列号计算目标路径：`AppDir/.arcmeta/vol_ABCD1234.db`。
- **内存同步 (Sync-to-Memory)**：
  1. 使用 `sqlite3_open` 打开上述路径的磁盘库（Disk DB）。
  2. 使用 `sqlite3_open(":memory:")` 创建内存镜像（Memory DB）。
  3. 执行 `sqlite3_backup` 快速填充。
- **落盘策略 (Disk Persistence)**：
  - 用户修改元数据时，仅对 Memory DB 进行 SQL 写入。
  - 启动 1.5s 防抖定时器，超时后通过 `sqlite3_backup` 将 Memory DB 反向写回 Disk DB。
  - 关机/拔盘时触发 `unmount`，强制执行一次全量备份。

## 3. 表结构与索引（精简版）

### 3.1 `vol_XXXX.db`
- **files**: 物理索引锚点。重点维护 `file_id_128` (PK), `frn`, `path`, `rating`, `color`, `mtime` 等。
- **palettes**: 色板数据表，通过 `file_id_128` 与 `files` 表关联。

### 3.2 `global.db`
- **categories**: 分类树结构。
- **category_items**: 关联表。字段包含 `category_id`, `file_id_128`, `vol_serial` (关键：用于跨库查询)。
- **tags**: 全局标签索引。

## 4. 分布式发现逻辑优化
- 硬盘中的离散 `.scch` 仅存储该磁盘的卷序列号信息。
- 当 ArcMeta 探测到目录中的 `.scch` 时，读取其卷序列号，若该序列号对应的 `vol_XXXX.db` 未在 `.arcmeta/` 目录下，则视为新库进行创建。

---
**旁观者意见**：该方案是纯净的、面向未来的重构。它彻底剥离了过时的文件系统同步逻辑，转而通过结构化的 SQL 体系进行管理。存储位置的收拢与属性的隐藏，确保了用户界面的“非侵入感”。
