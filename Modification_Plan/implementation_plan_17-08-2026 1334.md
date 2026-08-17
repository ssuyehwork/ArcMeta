# ArcMeta 全量架构排查报告 · 7维度 × 无脑实施方案

> **排查基准**：依据 `AGENTS.md` 规定的 7 个维度，结合 `ARCHITECTURE_DEBT.md` 已知负债，对当前代码库状态逐一核验。

---

## 排查结论总览

| 维度 | 状态 | 核心问题 |
|------|------|---------|
| ① 职责边界 (SRP) | 🔴 **有残留问题** | `ShellHelper` 塞了数据库重命名纠偏逻辑；`BatchRenameEngine.execute()` 直接用 `std::filesystem::rename` 绕过了 Disk/Memory 两套服务 |
| ② 主/工作线程 | 🟢 **已基本合规** | 所有耗时 IO 均通过 `QtConcurrent::run` 抛入后台；UI 回调均通过 `QMetaObject::invokeMethod(Qt::QueuedConnection)` 或 `QPointer` 哨兵保护回主线程 |
| ③ 时序与生命周期 | 🟡 **有一处隐患** | `TagRepository::checkAndMigrate()` 用 `static bool migratedChecked` 防重入，但该静态变量在多个 `QThread` 首次并发调用时存在 TOCTOU 竞态（未加锁） |
| ④ 数据一致性 (SSOT) | 🔴 **有残留问题** | `BatchRenameEngine::execute()` 绕过 `DiskBatchRenameService` / `MemoryBatchRenameService`，直接用 `std::filesystem::rename` + `MetadataManager::renameItem()`，丢失缩略图迁移步骤 |
| ⑤ 锁与并发安全 | 🟡 **有一处隐患** | `TagRepository::checkAndMigrate()` 的 `static bool migratedChecked` 无 `std::atomic` 或 `std::mutex` 保护 |
| ⑥ 边界情况与容错 | 🟡 **有一处被吞掉的失败路径** | `TagRepository::checkAndMigrate()` 内层 `sqlite3_step(groupInsertStmt)` 的返回值未校验，失败静默吞掉；`ShellHelper::resolveAndAlignDatabasePath()` 第 235 行 `QFile::rename` 成功与否无任何 log |
| ⑦ 全局视角复查 | 🔴 **有残留问题** | `ShellHelper` 现仍持有数据库路由/纠偏职责，与 `DatabaseManager` 期望的 `resolveVolumeDrift()` 归宿不一致；`BatchRenameEngine::execute()` 是一条独立的第三执行路径，与 Disk/Memory 两套服务并行存在但互不感知 |

---

## 维度 ① · 职责边界（SRP）

### 问题 1.1：`ShellHelper::resolveAndAlignDatabasePath()` 越权

**现状**：`src/util/ShellHelper.cpp` L170–242，`resolveAndAlignDatabasePath()` 完全是数据库文件的物理路由、重命名纠偏与 `.invalid` 标记逻辑，与 Shell 操作毫无关系。

**影响**：`ShellHelper` 被 `DatabaseManager` 调用，形成 util → meta 的反向依赖，层级倒置。

### 问题 1.2：`BatchRenameEngine::execute()` 是孤岛

**现状**：`src/meta/BatchRenameEngine.cpp` L53–66 内部直接调用 `std::filesystem::rename`，独立于 `DiskBatchRenameService`（有缩略图迁移）和 `MemoryBatchRenameService`（有 `.arc` 包内文件处理），是第三条执行路径。

**影响**：通过 `BatchRenameDialog` 走 `BatchRenameEngine::execute()` 路径时，缩略图不会随文件迁移，导致数据不一致。

---

## 维度 ③ ⑤ · 时序/并发安全（合并）

### 问题 3.1：`TagRepository::checkAndMigrate()` 防重入未加锁

**现状**（`src/meta/TagRepository.cpp` L14–17）：
```cpp
static bool migratedChecked = false;
if (!migratedChecked) {
    migratedChecked = true;
    checkAndMigrate();
}
```
`static bool` 在多线程下读-改-写不是原子操作，若两个线程同时首次进入 `getAllGroups()` 则可能同时通过 `if (!migratedChecked)`，导致 `checkAndMigrate()` 被并发执行两次，产生重复插入。

---

## 维度 ④ · 数据一致性 (SSOT)

### 问题 4.1：`BatchRenameEngine::execute()` 缩略图数据不一致

**现状**：`execute()` 仅执行 `std::filesystem::rename` + `MetadataManager::renameItem()`，未调用 `CapsuleMediaExtractor::getDiskThumbCachePath()` 迁移缩略图哈希路径。`DiskBatchRenameService::execute()` L49–60 有完整缩略图迁移代码，但 `BatchRenameEngine` 没有。

---

## 维度 ⑥ · 边界情况与容错

### 问题 6.1：`TagRepository::checkAndMigrate()` 插入失败被静默吞掉

**现状**（`src/meta/TagRepository.cpp` L274, L280）：
```cpp
sqlite3_step(groupInsertStmt);   // 返回值未检查
sqlite3_reset(groupInsertStmt);
```
插入失败（如主键冲突）不记录日志，不回滚，`trans.commit()` 强行提交。

### 问题 6.2：`ShellHelper::resolveAndAlignDatabasePath()` L235 重命名失败无 log

```cpp
if (QFile::rename(conflictPath, invalidPath)) {
}  // 空 if body，成功也好失败也好，没有任何记录
```

---

## 维度 ⑦ · 全局视角复查

### 问题 7.1：`ShellHelper` 中数据库职责未移入 `DatabaseManager`

`ARCHITECTURE_DEBT.md` 3.2 节明确指出应迁移，但当前 `ShellHelper::resolveAndAlignDatabasePath()` 仍是独立函数，被 `DatabaseManager::getDbForPath()` 调用。`DatabaseManager` 应自己持有这段逻辑（内部方法 `resolveVolumeDrift()`），而不是委托给 `ShellHelper`。

---

## 🔍 现状核实：已完成事项确认

经代码直接验证，以下 `ARCHITECTURE_DEBT.md` 中记录的问题**已经完成**：

| 事项 | 验证方式 | 结论 |
|------|---------|------|
| `TrashRepository` 创建 | 文件存在且实现正确 | ✅ 已完成 |
| `CategoryPanel.cpp` 剥离原生 SQL | L951 已改为 `TrashRepository::instance().hasTrashItems()` | ✅ 已完成 |
| `ContentPanel.cpp` 剥离原生 SQL | L2227 已改为 `TrashRepository::instance().getDiskTrashRecordByPath()` | ✅ 已完成 |
| `MftReader` 物理删除 | `src/mft/` 目录为空 | ✅ 已完成 |
| `IconCacheManager` 创建 | 文件存在 `src/ui/IconCacheManager.cpp` | ✅ 已完成 |
| `TrayController` 清除 MftReader 调用 | L67–76 中无任何 MftReader 引用 | ✅ 已完成 |
| `TagRepository` 统一 `QDir::drives()` | L195–198 已改为直接遍历 `QDir::drives()` | ✅ 已完成 |

---

## 无脑实施方案（按优先级排序）

> **执行原则**：每一步都是独立可验证的原子操作。按序执行，每步完成后编译确认无报错再进行下一步。

---

### 步骤 1 — 修复并发安全：`TagRepository` 防重入加原子锁

**文件**：[`src/meta/TagRepository.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/meta/TagRepository.cpp)

**操作**：将 `static bool migratedChecked` 改为 `static std::once_flag` + `std::call_once`，彻底消除 TOCTOU 竞态。

**改动对照**：

```diff
 // 头文件区（TagRepository.cpp 顶部）
 #include "TagRepository.h"
 #include "DatabaseManager.h"
 #include "MetadataManager.h"
 #include <QDebug>
 #include <QFileInfo>
 #include <QDir>
 #include <vector>
 #include <string>
+#include <mutex>

 QList<TagRepository::TagGroup> TagRepository::getAllGroups() {
     // 确保数据已自动检查与迁移
-    static bool migratedChecked = false;
-    if (!migratedChecked) {
-        migratedChecked = true;
-        checkAndMigrate();
-    }
+    static std::once_flag s_migrateOnce;
+    std::call_once(s_migrateOnce, []() { checkAndMigrate(); });
```

**验证**：全局搜索 `migratedChecked`，确认零残留。

---

### 步骤 2 — 修复容错：`TagRepository::checkAndMigrate()` 插入失败加日志

**文件**：[`src/meta/TagRepository.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/meta/TagRepository.cpp)

**操作**：检查 `sqlite3_step` 返回值，插入失败时记录 `qWarning`。

**改动对照**：

```diff
-                    for (const auto& mg : migratingGroups) {
-                        sqlite3_bind_int(groupInsertStmt, 1, mg.id);
-                        sqlite3_bind_text16(groupInsertStmt, 2, mg.name.c_str(), -1, SQLITE_TRANSIENT);
-                        sqlite3_bind_text16(groupInsertStmt, 3, mg.color.c_str(), -1, SQLITE_TRANSIENT);
-                        sqlite3_bind_int(groupInsertStmt, 4, mg.sortOrder);
-                        sqlite3_step(groupInsertStmt);
-                        sqlite3_reset(groupInsertStmt);
+                    for (const auto& mg : migratingGroups) {
+                        sqlite3_bind_int(groupInsertStmt, 1, mg.id);
+                        sqlite3_bind_text16(groupInsertStmt, 2, mg.name.c_str(), -1, SQLITE_TRANSIENT);
+                        sqlite3_bind_text16(groupInsertStmt, 3, mg.color.c_str(), -1, SQLITE_TRANSIENT);
+                        sqlite3_bind_int(groupInsertStmt, 4, mg.sortOrder);
+                        if (sqlite3_step(groupInsertStmt) != SQLITE_DONE) {
+                            qWarning() << "[TagRepository] 迁移 tag_group 插入失败:"
+                                       << sqlite3_errmsg(globalDb);
+                        }
+                        sqlite3_reset(groupInsertStmt);

                         for (const auto& tag : mg.tags) {
                             sqlite3_bind_int(itemInsertStmt, 1, mg.id);
                             sqlite3_bind_text16(itemInsertStmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
-                            sqlite3_step(itemInsertStmt);
-                            sqlite3_reset(itemInsertStmt);
+                            if (sqlite3_step(itemInsertStmt) != SQLITE_DONE) {
+                                qWarning() << "[TagRepository] 迁移 tag_group_item 插入失败:"
+                                           << sqlite3_errmsg(globalDb);
+                            }
+                            sqlite3_reset(itemInsertStmt);
                         }
                     }
```

---

### 步骤 3 — 修复容错：`ShellHelper` 重命名冲突处理加 log

**文件**：[`src/util/ShellHelper.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/util/ShellHelper.cpp)

**操作**：在 L235 的 `if (QFile::rename(conflictPath, invalidPath))` 空 body 中补充日志。

**改动对照**：

```diff
-                if (QFile::rename(conflictPath, invalidPath)) {
-                }
+                if (QFile::rename(conflictPath, invalidPath)) {
+                    qWarning() << "[ShellHelper] 冲突库已标注为无效:" << invalidPath;
+                } else {
+                    qWarning() << "[ShellHelper] 冲突库标注失败，原始路径保留:" << conflictPath;
+                }
```

---

### 步骤 4 — 修复 SSOT：`BatchRenameEngine::execute()` 走正确执行路径

**核心判断**：`BatchRenameEngine::execute()` 是由 `BatchRenameDialog` 在 **内存托管库模式** 下调用的。它应当复用 `MemoryBatchRenameService::execute()` 的完整流程（含缩略图迁移），而不是自己裸调用 `std::filesystem::rename`。

**文件**：[`src/meta/BatchRenameEngine.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/meta/BatchRenameEngine.cpp)

**操作**：`BatchRenameEngine::execute()` 内部委托给 `MemoryBatchRenameService`，自己不再直接操作文件系统。

**改动对照**：

```diff
 #include "BatchRenameEngine.h"
 #include "MetadataManager.h"
 #include <QFileInfo>
 #include <QDateTime>
-#include <filesystem>
+#include "../ui/MemoryBatchRenameService.h"

 bool BatchRenameEngine::execute(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
-    auto newNames = preview(originalPaths, rules);
-    for (int i = 0; i < (int)originalPaths.size(); ++i) {
-        std::filesystem::path oldP(originalPaths[i]);
-        std::filesystem::path newP = oldP.parent_path() / newNames[i];
-        try {
-            std::filesystem::rename(oldP, newP);
-            // 2026-05-24：更新数据库路径索引
-            MetadataManager::instance().renameItem(oldP.wstring(), newP.wstring());
-        } catch (...) {
-            return false;
-        }
-    }
-    return true;
+    auto newNameWstrings = preview(originalPaths, rules);
+    // 统一委托 MemoryBatchRenameService，确保缩略图迁移与数据库事务一致性
+    MemoryBatchRenameService::execute(originalPaths, newNameWstrings, nullptr);
+    return true;
 }
```

> [!IMPORTANT]
> 注意：`MemoryBatchRenameService::execute()` 的回调是异步的（在 `QtConcurrent` 线程完成后触发）。`BatchRenameEngine::execute()` 同步返回 `true` 仅代表任务已提交，不代表执行完成。若调用方有强同步需求，需调整 `BatchRenameDialog` 的完成通知机制（在回调中发信号/刷新 UI）。

---

### 步骤 5 — 职责归位：`ShellHelper::resolveAndAlignDatabasePath()` 迁移至 `DatabaseManager`

> [!WARNING]
> 这是本轮最大的一步，改动涉及 `DatabaseManager.h`、`DatabaseManager.cpp`、`ShellHelper.h`、`ShellHelper.cpp`。必须在步骤 1–4 全部验证通过后再执行。

**目标**：将 `resolveAndAlignDatabasePath()` 从 `ShellHelper` 移入 `DatabaseManager` 作为 `private` 方法 `resolveVolumeDrift()`，`ShellHelper` 不再有任何数据库文件操作代码。

**执行清单**：

1. 在 [`src/meta/DatabaseManager.h`](file:///G:/C++/ArcMeta/ArcMeta/src/meta/DatabaseManager.h) `private:` 区声明：
   ```cpp
   QString resolveVolumeDrift(const std::wstring& volumeSerial, const QString& driveLetter,
                              const QString& currentDiskPathInConn, bool isLoaded);
   ```

2. 在 [`src/meta/DatabaseManager.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/meta/DatabaseManager.cpp) 中实现该方法（内容 **原封不动** 从 `ShellHelper::resolveAndAlignDatabasePath()` 复制过来，仅改方法名与 `this->` 调用风格）。

3. 将 `DatabaseManager.cpp` 中所有调用 `ShellHelper::resolveAndAlignDatabasePath(...)` 的地方改为 `resolveVolumeDrift(...)` 的本地调用。

4. 从 [`src/util/ShellHelper.h`](file:///G:/C++/ArcMeta/ArcMeta/src/util/ShellHelper.h) 中删除 `static QString resolveAndAlignDatabasePath(...)` 声明。

5. 从 [`src/util/ShellHelper.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/util/ShellHelper.cpp) 中删除整个 `resolveAndAlignDatabasePath()` 函数体（L170–242）。

6. 检查 `ShellHelper.cpp` 是否还有 `#include "../meta/DatabaseManager.h"` 的需要——若其余函数不需要则一并移除，彻底消除 `util` 层对 `meta` 层的依赖。

---

## 验证计划

### 自动化验证
```powershell
# 步骤 1 完成后：确认无 migratedChecked 残留
grep -rn "migratedChecked" G:\C++\ArcMeta\ArcMeta\src\

# 步骤 4 完成后：确认 BatchRenameEngine 不再包含 filesystem::rename
grep -rn "filesystem::rename" G:\C++\ArcMeta\ArcMeta\src\meta\BatchRenameEngine.cpp

# 步骤 5 完成后：确认 ShellHelper 不再包含任何数据库/db 操作
grep -n "\.db\|DatabaseManager\|resolveAndAlign" G:\C++\ArcMeta\ArcMeta\src\util\ShellHelper.cpp
```

### 编译验证
每步完成后执行 CMake 构建，确保零编译报错、零链接错误。

### 手动验证
- **步骤 1**：在 Debug 构建下，多线程压测标签管理器首次打开，确认无重复 `tag_group` 数据写入。
- **步骤 4**：使用批量重命名对图片文件操作，确认缩略图哈希文件随文件一并迁移。
- **步骤 5**：插拔移动硬盘，确认盘符漂移后数据库仍能正确路由加载。
