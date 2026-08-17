# Plan-130 实施后审核 · 修正方案

> **审核时间**：2026-08-17
> **审核基准**：对比 `implementation_plan.md` 5 个步骤，逐一验证实际代码状态。

---

## 验收结论总览

| 步骤 | 预期 | 实际 | 结论 |
|------|------|------|------|
| 步骤1：`TagRepository` 防重入改 `once_flag` | `static std::once_flag` + `std::call_once` | ✅ 已按预期实施 | **PASS** |
| 步骤2：`TagRepository` 插入失败加 `qWarning` | `sqlite3_step != SQLITE_DONE` 时 warn | ✅ 已按预期实施 | **PASS** |
| 步骤3：`ShellHelper` 重命名冲突加 log | 空 `if` body 补充 warn/else | ✅ 已在 `DatabaseManager::resolveVolumeDrift` 中实现（函数已迁移） | **PASS** |
| 步骤4：`BatchRenameEngine::execute()` 委托 `MemoryBatchRenameService` | 删除 `std::filesystem::rename`，调用 `MemoryBatchRenameService::execute` | ⚠️ **功能正确但引入架构违规** | **FAIL** |
| 步骤5：`resolveAndAlignDatabasePath` 迁移至 `DatabaseManager` | 从 `ShellHelper` 删除，在 `DatabaseManager` 以 `private` 方法实现 | ⚠️ **主体完成但残留 `ShellHelper::ensureHidden` 依赖** | **PARTIAL** |

---

## 问题 A：`meta` 层反向依赖 `ui` 层（严重架构违规）

### 现状
`src/meta/BatchRenameEngine.cpp` L3：
```cpp
#include "../ui/MemoryBatchRenameService.h"
```
`meta/` 层包含 `ui/` 层头文件，产生了**向上依赖**（低层依赖高层），是 MVC 架构的严重倒置。`MemoryBatchRenameService` 本身也在 `src/ui/` 下，进一步证明分层边界混乱。

### 根因分析
实施方案中的指令存在设计缺陷：`MemoryBatchRenameService` 虽然头文件简单（无 Qt GUI 依赖），但**物理位置在 `src/ui/`**，`meta` 层不允许 `#include` 它。

### 正确根治方案
`BatchRenameEngine::execute()` 不应委托给任何 UI 服务。正确做法是：

**`BatchRenameEngine::execute()` 的职责本就只是"规则计算 + 物理改名"**，缩略图迁移属于 `DiskBatchRenameService`（磁盘模式）的职责范围，内存模式（`.arc` 包）由 `MemoryBatchRenameService` 负责。两者都是 **UI 调度层**，由 `BatchRenameDialog` 在调用前选择正确路径——而 `BatchRenameEngine::execute()` 应当**仅负责计算新名称列表并返回**，不负责物理执行文件系统操作。

#### 具体实施方案

**① 在 `BatchRenameEngine.h` 中废弃 `execute()`，改为只保留 `preview()`**

```cpp
// BatchRenameEngine.h — execute() 签名不变，但实现回归原来职责：
// 仅执行"规则计算 → 委托 MetadataManager::renameBatchAsync 更新索引"
// 物理文件操作由上层调用者（BatchRenameDialog）按模式分发
```

**② `BatchRenameEngine.cpp` 恢复为不依赖 UI 层的版本：**

```diff
 #include "BatchRenameEngine.h"
 #include "MetadataManager.h"
 #include <QFileInfo>
 #include <QDateTime>
-#include "../ui/MemoryBatchRenameService.h"
+#include <filesystem>

 bool BatchRenameEngine::execute(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
-    auto newNameWstrings = preview(originalPaths, rules);
-    MemoryBatchRenameService::execute(originalPaths, newNameWstrings, nullptr);
-    return true;
+    auto newNames = preview(originalPaths, rules);
+    std::vector<std::pair<std::wstring, std::wstring>> rawPairs;
+    for (int i = 0; i < (int)originalPaths.size(); ++i) {
+        std::filesystem::path oldP(originalPaths[i]);
+        std::filesystem::path newP = oldP.parent_path() / newNames[i];
+        try {
+            std::filesystem::rename(oldP, newP);
+            rawPairs.push_back({oldP.wstring(), newP.wstring()});
+        } catch (...) {
+            // 单文件失败不中断整批，继续处理
+        }
+    }
+    // 批量更新数据库路径索引（异步，不阻塞）
+    if (!rawPairs.empty()) {
+        MetadataManager::instance().renameBatchAsync(rawPairs, nullptr);
+    }
+    return true;
 }
```

> [!IMPORTANT]
> **关于缩略图**：`BatchRenameEngine` 由 `BatchRenameDialog` 调用，对应的是**内存托管库模式**下的用户自定义规则批量重命名。在此模式下，缩略图存储路径基于 FID（文件 ID）而非物理路径，**与物理文件名无关**，故不需要迁移缩略图哈希路径——缩略图迁移只在 `DiskBatchRenameService`（纯磁盘模式）中才需要。原方案中强行委托 `MemoryBatchRenameService` 反而多余，且制造了架构违规。

**③ 验证**：
```powershell
grep -n "MemoryBatchRenameService" G:\C++\ArcMeta\ArcMeta\src\meta\BatchRenameEngine.cpp
# 预期：No results found
```

---

## 问题 B：`DatabaseManager::resolveVolumeDrift()` 仍依赖 `ShellHelper`

### 现状
`src/meta/DatabaseManager.cpp` L759：
```cpp
ShellHelper::ensureHidden(metaDir.toStdWString());
```
`DatabaseManager.cpp` L11：
```cpp
#include "../util/ShellHelper.h"
```
迁移后 `resolveVolumeDrift` 虽然已归入 `DatabaseManager`，但函数体内依然调用了 `ShellHelper::ensureHidden()`，导致 `meta` 层仍依赖 `util` 层。

> [!NOTE]
> L131 已有注释：`🚀【修改方案一】：彻底删去对 ShellHelper::ensureHidden 的直接耦合`，说明开发者已知晓此问题但尚未实施。

### 正确根治方案
`ensureHidden` 只是一行 Win32 API 调用，职责极简，直接内联到 `DatabaseManager.cpp` 中，彻底消除对 `ShellHelper.h` 的依赖。

**① 在 `DatabaseManager.cpp` 顶部添加内联隐藏函数（匿名命名空间）：**

```diff
 // DatabaseManager.cpp 顶部 includes 区之后，namespace ArcMeta { 之前
+namespace {
+#ifdef Q_OS_WIN
+    inline void ensureHidden(const std::wstring& path) {
+        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
+    }
+#else
+    inline void ensureHidden(const std::wstring&) {}
+#endif
+} // anonymous namespace
```

**② 删除 `DatabaseManager.cpp` 对 `ShellHelper.h` 的 include：**

```diff
-#include "../util/ShellHelper.h"
```

> [!IMPORTANT]
> 删除前须全局搜索 `ShellHelper` 在 `DatabaseManager.cpp` 中的所有出现点，确认除 `ensureHidden` 外无其他调用。当前已确认 L11（include）和 L759（`ensureHidden` 调用）是仅有的两处，移除后 `DatabaseManager` 与 `ShellHelper` 完全解耦。

**③ 验证：**
```powershell
grep -n "ShellHelper" G:\C++\ArcMeta\ArcMeta\src\meta\DatabaseManager.cpp
# 预期：No results found

grep -n "ShellHelper" G:\C++\ArcMeta\ArcMeta\src\meta\DatabaseManager.h
# 预期：No results found（已确认为空）
```

---

## 问题 C：`call_once` lambda 调用静态成员函数语法确认

### 现状
`TagRepository.cpp` L16：
```cpp
std::call_once(s_migrateOnce, []() { checkAndMigrate(); });
```
`checkAndMigrate()` 是 `TagRepository` 的 `static` 成员函数（`TagRepository.h` L31 确认）。

### 判断
在 `static` 成员函数（`getAllGroups()`）的上下文中，`checkAndMigrate()` 无需前缀即可直接调用，**语法合法，无编译风险**。此处 **PASS**，无需修改。

---

## 执行顺序

两个问题相互独立，可并行执行：

```
问题 A（BatchRenameEngine 去除 ui 层依赖）
问题 B（DatabaseManager 去除 ShellHelper 依赖）
```

### 每步完成后验证命令

```powershell
# A 完成后
grep -rn "MemoryBatchRenameService" G:\C++\ArcMeta\ArcMeta\src\meta\

# B 完成后
grep -n "ShellHelper" G:\C++\ArcMeta\ArcMeta\src\meta\DatabaseManager.cpp

# 全部完成后——确认 meta 层无任何 ui 层头文件引用
grep -rn "#include.*\.\./ui/" G:\C++\ArcMeta\ArcMeta\src\meta\
```

最后执行 CMake 构建，确认零编译报错。
