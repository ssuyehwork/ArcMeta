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

## 4. JSON 方式残留说明
- **底层格式**: 目前 `.scch` 文件内部依然使用 JSON 文本格式。如果“废除 JSON”包含格式升级，则应考虑将 `AmMetaScch` 的序列化改为二进制。
- **配置/历史记录**: `ScanDialog.cpp` 使用 JSON 存储搜索历史。考虑到其体量极小且不涉及核心元数据，可暂时保留，或统一迁移至 `AppConfig`。

## 5. 下一步行动建议
1. **物理清理**: 删除 `src/db/SyncEngine.cpp/h` 和 `src/db/FolderRepo.h`。
2. **位置迁移**: 将 `CategoryRepo` 和 `FavoritesRepo` 移动到 `src/meta/` 目录下，彻底移除 `src/db/` 文件夹。
3. **断开日志**: 修改 `MetadataManager.cpp`，停止生成 `Synchronize.scch`。
4. **编译测试**: 在清理引用后执行一次全量编译，确保没有因删除数据库类而遗漏的调用点。
