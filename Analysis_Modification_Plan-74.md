# Analysis and Modification Plan - 74

## 1. 任务背景
当前版本已明确废除基于 SQLite 的 DB 类数据库以及旧有的 JSON 存储方式，全面转向 SCCH (Small Compact Cached Hierarchy) 架构。本任务旨在深度排查代码库中残留的 DB 逻辑与 JSON 相关代码，并提供详尽的重构方案，确保系统逻辑的纯净性。

## 2. 核心残留逻辑排查结果

### 2.1 数据库持久层 (src/db/)
尽管 `CategoryRepo` 和 `FavoritesRepo` 已经重构为使用 SCCH，但该目录下仍存在大量完全服务于 SQLite 的代码：
- **Database.cpp / .h**: 数据库连接池、WAL 模式配置、表结构创建（`createTables`）、索引创建。这些是 DB 模式的核心，在纯 SCCH 模式下已无存在意义。
- **ItemRepo.cpp / .h**: 包含 `save`, `saveBasicInfo`, `searchByKeyword`, `getPathsBySystemType` 等大量 SQL 操作。其中 `save` 方法依然在被 `SyncEngine` 调用。
- **FolderRepo.cpp / .h**: 包含文件夹元数据在数据库中的增删改查。
- **SyncEngine.cpp / .h**: 目前的同步引擎逻辑（`runIncrementalSync`, `runFullScan`）的核心是将物理 `.scch` 文件的数据同步“回填”到 SQLite 数据库中。这属于最严重的逻辑残留。

### 2.2 元数据管理层 (src/meta/)
- **MetadataManager.cpp**:
    - `persistAsync` 方法中依然调用了 `addToSyncLog`。
    - `addToSyncLog` 会在磁盘生成 `Synchronize.scch`，触发 `SyncEngine` 将数据写入 DB。
    - 依然保留了 `getVolumeSerialNumber` 等可能仅为 DB 索引服务的辅助逻辑（虽然 SCCH 也可能需要卷序列号，但目前主要用于数据库主键合成）。

### 2.3 JSON 方式残留
- **数据库内部 JSON**: `ItemRepo.cpp` 和 `FolderRepo.cpp` 中将标签（tags）和调色板（palettes）序列化为 JSON 字符串存入数据库字段。
- **独立配置文件**: `Synchronize.scch` 和 `All_FRN_metadata.scch` 虽然后缀为 `.scch`，但内部实现依然是 `QJsonDocument` 序列化的明文 JSON。如果“废除 JSON 方式”包含序列化格式的改变（如转向 Protobuf 或自定义二进制），则这些也属于残留。

## 3. 详细修改方案

### 3.1 彻底剥离数据库依赖
1. **废弃文件**: 
   - 建议直接从编译单元中移除并物理删除：`src/db/Database.cpp`, `src/db/Database.h`, `src/db/ItemRepo.cpp`, `src/db/ItemRepo.h`, `src/db/FolderRepo.cpp`, `src/db/FolderRepo.h`。
2. **重构 `CategoryRepo` 与 `FavoritesRepo`**:
   - 移除文件中所有关于“彻底废除数据库”的说明注释（因为已经废除了，不再需要提醒）。
   - 清理其中为了对齐旧数据库而保留的冗余接口，例如 `getUniqueItemCount` 和 `getUncategorizedItemCount` 目前返回硬编码的 `0`，应在 UI 层也相应取消对这些接口的依赖。

### 3.2 改造同步引擎 (`SyncEngine`)
- **逻辑重定位**: `SyncEngine` 不应再负责“SCCH -> DB”的同步。
- **新职责**: 如果需要保持“全量扫描”功能，`SyncEngine` 的新职责应是扫描磁盘上的 `metadata.scch` 文件并建立**运行时内存索引**或生成**全局分类快照**，而不是写入 SQLite。
- **删除 `rebuildTagStats`**: 该函数完全基于 SQL 查询，应改为遍历所有已加载的 `AmMetaScch` 对象来统计标签。

### 3.3 修改 `MetadataManager` 持久化链路
1. **断开同步日志**: 在 `persistAsync` 中移除 `addToSyncLog` 调用。
2. **强化内存缓存**: 确保 `m_cache` 是系统查询元数据的唯一真理来源。
3. **移除数据库初始化**: 确保 `MetadataManager` 不再调用 `Database::instance().init()`。

### 3.4 统一 SCCH 存储协议
- 如果用户要求的“废除 JSON 方式”是指底层存储格式，则需要修改 `AmMetaScch::save()`，使用二进制流（如 `QDataStream`）替代 `QJsonDocument`。
- 目前 `AmMetaScch.cpp` 依然大量使用 `QJsonObject` 作为中间转换格式，建议定义强类型的二进制序列化协议。

## 4. 总结建议
修改后的架构应呈现为：
- **UI 层** -> 调用 **MetadataManager / CategoryRepo**。
- **MetadataManager** -> 维护 **Runtime Cache**，异步读写 **AmMetaScch** (离散 `.scch` 文件)。
- **CategoryRepo / FavoritesRepo** -> 直接操作 **arcmeta_categories.scch** 和 **arcmeta_favorites.scch**。
- **不再有任何 SQL 语句，不再有任何 `.db` 或 `.sqlite` 文件生成。**
