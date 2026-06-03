# Analysis and Modification Plan - 75

## 1. 任务背景
在用户手动删除了 `Database.cpp/h`, `ItemRepo.cpp/h`, `Database.cpp/h` 后，系统已基本切断了与 SQLite 的物理连接。本阶段任务是继续排查 `src` 目录下剩余的冗余文件与逻辑残留，确保代码库完全对齐“纯 SCCH 架构”。

## 2. 冗余文件与头文件残留排查

### 2.1 物理文件残留
- **src/db/SyncEngine.cpp / .h**:
    - 该文件依然存在，且其 90% 的逻辑（如 `runIncrementalSync`, `runFullScan`, `rebuildTagStats`）都是为了将数据从物理 SCCH 同步到已删除的数据库中。
    - **修改建议**: 彻底删除这两个文件。如果需要保留“全量扫描”功能，应在 `src/meta/` 目录下重新实现一个仅针对 SCCH 的扫描器。
- **src/db/FolderRepo.h**:
    - 虽然 `.cpp` 已删除，但头文件依然残留，且其中包含对已不存在的 `Database.h` 的引用。
    - **修改建议**: 物理删除该头文件。

### 2.2 无效的引用 (Includes)
- **src/db/SyncEngine.cpp**:
    - 引用了 `Database.h`, `ItemRepo.h`, `FolderRepo.h`。这些引用将导致编译失败。

## 3. 逻辑残留排查 (逻辑耦合点)

### 3.1 MetadataManager 链路残留
- **Synchronize.scch (事务日志)**:
    - `MetadataManager::persistAsync` 依然在调用 `addToSyncLog`。
    - 在纯 SCCH 模式下，不需要通过事务日志来通知 `SyncEngine` 同步数据库。
    - **修改建议**: 在 `persistAsync` 中移除所有与 `SyncLog` 相关的调用。

### 3.2 UI 与 模型层的残留
- **CategoryModel.cpp**:
    - 依然包含 `#include "../db/FavoritesRepo.h"`。
    - 虽然 `FavoritesRepo` 已改用 SCCH 存储，但建议将其移动到 `src/meta/` 目录下，以符合其“元数据管理”的本质，并清理 `src/db/` 目录。
- **CategoryRepo.cpp**:
    - 保留了 `getUniqueItemCount()` 和 `getUncategorizedItemCount()` 等接口，返回硬编码的 `0`。
    - **修改建议**: 检查 UI 层（如 `CategoryModel`）是否依然调用这些接口，若有则应一并移除对应的 UI 显示逻辑。

## 4. JSON 方式残留说明与格式演进方案

### 4.1 SCCH 底层格式现状
虽然系统在文件名上已经废弃了 `.json`，但通过对 `src/meta/AmMetaScch.cpp` 的深度审计发现：
- **序列化引擎**: 依然重度依赖 `QJsonDocument`, `QJsonObject` 和 `QJsonArray`。
- **物理表现**: 磁盘上的 `.scch` 文件依然是 UTF-8 编码的明文 JSON 字符串。
- **性能瓶颈**: 对于包含数万个条目的文件夹，JSON 的解析与字符串拼接开销（`toQString` / `toStdWString`）会随着元数据规模增加而呈线性增长。

### 4.2 “完全废除 JSON”的演进路径
若要彻底对齐用户“废除 JSON”的要求，建议执行以下重构：
1. **引入二进制序列化**: 参考 `CacheManager.h` 中的 `CacheHeader` 结构，为 `.scch` 文件定义定长头部 + 紧凑型条目块 + 字符串偏移池的格式。
2. **重构 AmMetaScch**: 移除所有 `QJson` 系列头文件引用，改用 `QDataStream` 或内存映射（MMap）进行原始字节流读写。
3. **数据对齐**: 将 `PaletteEntry` 等结构体直接序列化为二进制，取消目前将其转换为 JSON 数组的低效中间环节。

### 4.3 UI 辅助逻辑排查
- **ScanDialog.cpp**:
    - 依然在使用 `QJsonDocument` 处理搜索历史。这虽然不属于元数据核心，但也属于“JSON 逻辑残留”。建议将其存储逻辑迁移至 `AppConfig`（基于 `QSettings`），彻底清除 UI 层的 `QJson` 依赖。

## 5. 下一步行动建议
1. **物理清理**: 删除 `src/db/SyncEngine.cpp/h` 和 `src/db/FolderRepo.h`。
2. **位置迁移**: 将 `CategoryRepo` 和 `FavoritesRepo` 移动到 `src/meta/` 目录下，彻底移除 `src/db/` 文件夹。
3. **断开日志**: 修改 `MetadataManager.cpp`，停止生成 `Synchronize.scch`。
4. **编译测试**: 在清理引用后执行一次全量编译，确保没有因删除数据库类而遗漏的调用点。
