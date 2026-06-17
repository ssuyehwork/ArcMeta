# 多维隔离索引架构与卷生命周期管理 —— Analysis_Modification_Plan-55.md

## 1. 任务背景
为了提升系统检索性能并彻底消除文件与文件夹同名导致的逻辑混淆，需要在 `MetadataManager` 中实现一套物理隔离的倒排索引架构。该架构需支持：
- **文件与文件夹隔离**：通过独立映射表管理。
- **后缀名极速索引**：支持全盘特定格式文件的瞬时查找。
- **卷生命周期感知**：在驱动器挂载/卸载时自动维护索引完整性，防止内存溢出。

## 2. 核心架构设计

### 2.1 隔离式倒排索引
在 `m_mutex` (shared_mutex) 保护下引入三个 `unordered_map`：
1. `m_fileNameToFids`: 键为文件名（含后缀），值为 FID 列表。
2. `m_folderNameToFids`: 键为文件夹名，值为 FID 列表。
3. `m_extensionToFids`: 键为小写后缀（不含点），值为 FID 列表。

### 2.2 卷管理机制
- **FID 格式解析**：通过 `FRN:VOL_SERIAL:HEX` 提取卷序列号。
- **动态装载/卸载**：
    - `loadVolumeNameCache(volSerial)`: 从主缓存 `m_cache` 批量提取并注册。
    - `unloadVolumeNameCache(volSerial)`: 根据 FID 前缀物理移除相关记录，并清理空键。

### 2.3 流程集成点
- **初始化/激活**：新项注入时自动派发至对应索引，并执行 FID 去重检查。
- **重命名**：原子化移除旧索引并注册新索引。
- **删除/冲突处理**：同步清理倒排索引，确保“物理对齐”。

## 3. 强制对照表
| 编号 | 用户原话/理解 | 我的方案对应点 | 是否一致 |
| :--- | :--- | :--- | :--- |
| 1 | 将文件、文件夹、后缀名在索引中物理隔离 | 实现 m_fileNameToFids, m_folderNameToFids, m_extensionToFids | 是 |
| 2 | 后缀名建立独立关联，实现极速检索 | 实现 m_extensionToFids 及其查询 API | 是 |
| 3 | 卸载/插入驱动器时自动维护索引 (Self-Healing) | 实现 unloadVolumeNameCache 和 loadVolumeNameCache | 是 |
| 4 | renameItem 需处理后缀名变更 | 在 renameItem 中集成名称与后缀的重索引逻辑 | 是 |
| 5 | 使用 C++17 标准，确保线程安全 | 全量采用 shared_mutex 保护，修复 C++20 兼容性问题 | 是 |
