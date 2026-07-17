# 物理监控目录自动同步分类与实时刷新 —— Modification_Plan-9.md

## 1. 任务背景
在项目当前架构中，为了确保极限稳定性，已经停用了基于 USN Journal 的对账监听（`MftReader` 监听处于注销状态），所有物理变更监控完全收拢在 `NativeFolderWatcher` (IOCP) 这一条主轨上。
然而，当用户在内容容器中对文件夹执行“迁移”移入托管库 `ArcMeta.Library_[盘符]`（或在托管库/自定义监控目录内创建、移入新文件夹）时，`NativeFolderWatcher` 检测到目录增加后仅仅触发了路径登记（`MetadataManager::instance().markAsRegistered`）。该调用只将项目路径记录进 `metadata` 表，完全没有在数据库的 `categories` 分类表里创建条目。这导致对应的分类树节点无法在侧边栏自动生成，用户必须手动重启整个主程序来触发自启动同步。

本方案旨在打通这一核心断层，实现物理目录变动到侧边栏分类树的实时、无感自动生成与刷新。

## 2. 问题定位
- **断层所在**：`src/core/NativeFolderWatcher.cpp` 第 191-195 行：
  ```cpp
                  if (info.isDir()) {
                      // 目录：触发登记（内部已优化为异步，见 MetadataManager::markAsRegistered）
                      qDebug() << "[Watcher] 检测到目录级变动，触发级联登记";
                      MetadataManager::instance().markAsRegistered(fullPath);
                  }
  ```
  - `markAsRegistered` 仅仅执行了文件元数据的登记事务，并不具备在 SQLite 中进行物理到分类映射、生成 category DB 记录的能力。
  - 核心的镜像分类生成和级联树创建完全由 `AutoImportManager::handleRecursiveIngestion` 实现。由于没有人在 IOCP 触发该过程，导致自动创建分类失效。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将文件夹迁移到 ArcMeta.Library_[盘符] 托管库后，侧边栏能够自动同步创建和刷新分类 | 在 `NativeFolderWatcher::handleNotification` 中，检测到目录级别变化时，由 `markAsRegistered` 替换为异步拉起 `AutoImportManager::handleRecursiveIngestion(fullPath)` 对账 | ✅ 一致 |
| 2    | 不需要重启主程序，应该立即自动刷新 | `handleRecursiveIngestion` 在后台对账完成后会调用 `notifyFullUIRebuild()`，向 GUI 发送 `"__RELOAD_ALL__"`，从而触发 `CategoryPanel` 实时全量重载，实现免重启刷新 | ✅ 一致 |

---

## 4. 详细解决方案

### 4.1 引入异步分类构建与实时对账机制
修改 `src/core/NativeFolderWatcher.cpp` 中的 `handleNotification`。在捕获到新增/改名/修改动作（`FILE_ACTION_ADDED` 等）且实体为目录（`info.isDir()`）时，直接通过 `QtConcurrent::run` 异步调度分类映射构建与级联同步函数 `AutoImportManager::instance().handleRecursiveIngestion(fullPath)`：

**修改前 (`src/core/NativeFolderWatcher.cpp` 第 191-195 行)：**
```cpp
                if (info.isDir()) {
                    // 目录：触发登记（内部已优化为异步，见 MetadataManager::markAsRegistered）
                    qDebug() << "[Watcher] 检测到目录级变动，触发级联登记";
                    MetadataManager::instance().markAsRegistered(fullPath);
                }
```

**修改后：**
```cpp
                if (info.isDir()) {
                    // 目录：触发级联对账与分类树构建（统一交由 handleRecursiveIngestion 执行）
                    // 2026-07-xx 按照 Plan-128: 对目录级变动，统一调用 handleRecursiveIngestion 实现 1:1 分类树自动创建与全量重构刷新
                    qDebug() << "[Watcher] 检测到目录级变动，触发级联对账与 1:1 分类树构建";
                    (void)QtConcurrent::run([fullPath]() {
                        AutoImportManager::instance().handleRecursiveIngestion(fullPath);
                    });
                }
```

同时，必须在 `src/core/NativeFolderWatcher.cpp` 的头部引入 `AutoImportManager.h` 和 `<QtConcurrent>` 头文件。

---

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [x] 模块/文件：`src/core/NativeFolderWatcher.cpp`
  - 引入 `AutoImportManager.h` 和 `<QtConcurrent>`
  - 将目录级变化的 `markAsRegistered` 处理逻辑修改为 `AutoImportManager::handleRecursiveIngestion`

**明确禁止越界修改的范围：**
- [ ] 严禁修改任何 UI 树加载结构、排序、元数据编辑限制或其他不相关的底层数据层。

---

## 6. 实现准则与预警【核心】

1. **头文件依赖**：确保在 `NativeFolderWatcher.cpp` 中通过 `#include "AutoImportManager.h"` 与 `#include <QtConcurrent>` 正确建立链接。
2. **并发安全性**：由于 `NativeFolderWatcher` 接收文件系统高频的底层变更信号，`AutoImportManager::handleRecursiveIngestion` 内部已具备完善的 `recursive_mutex` 独占机制与 `setInternalOperating` 高频抑制机制，异步拉起它是绝对安全、高性能且防抖的。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨机制 | 物理路径模式与侧边栏分类模式分离；监控 for 'ArcMeta.Library_[Drive]' folders has transitioned back to IOCP-based NativeFolderWatcher. | ✅ 符合。本次完全利用并维护 NativeFolderWatcher IOCP 链，实现极致的对账刷新。 |
| 侧边栏刷新 | 提供无重启、自动重载机制。 | ✅ 符合。利用已有的 notifyFullUIRebuild 触发 __RELOAD_ALL__，实现完美的自动化界面一致性。 |
