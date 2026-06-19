# 非管理员模式下的逻辑架构降级方案 —— Analysis_Modification_Plan-62.md

## 1. 核心矛盾分析
当前 ArcMeta 的核心性能优势源于其对 NTFS 卷 **MFT (Master File Table)** 的直接解析以及对 **USN (Update Sequence Number)** 日志的监听。
- **技术红线**：在 Windows 操作系统中，访问 `\\.\PhysicalDriveX` 或卷设备 `\\.\C:` / `\\.\D:` 需要 `GENERIC_READ` 权限，这在 UAC 机制下强制要求管理员身份。**无论该分区是否为系统盘（C盘），读取其底层元数据表的权限要求是一致的。**

## 2. 移除管理员权限的技术代价
如果主程序以普通用户权限启动，将产生以下连锁反应：
1.  **MFT 扫描失效**：`MftReader` 无法打开卷句柄，导致全盘极速索引功能崩溃。
2.  **USN 监控失效**：`UsnWatcher` 无法监听文件变动，实时增量同步功能失效。
3.  **权限申请失败**：`SE_BACKUP_NAME` 等特权启用将返回错误。

## 3. 混合动力/自动退化架构方案
为了满足用户“不使用管理员权限”的需求，建议在 `CoreController` 中引入一套“退化（Fallback）”逻辑：

### 3.1 权限探测与引擎分流
在程序启动阶段探测当前权限：
- **管理员权限**：激活 `MftReader` (极速模式)。
- **普通用户权限**：激活 `StandardScanEngine` (常规模式)。

### 3.2 常规模式下的技术实现
- **扫描引擎**：使用 `QDirIterator` 或多线程 `std::filesystem::recursive_directory_iterator` 进行递归磁盘遍历。
- **监控引擎**：使用 `QFileSystemWatcher` 或 Win32 `ReadDirectoryChangesW` 替代 USN 日志。
- **元数据存储**：由于没有 MFT 的 FRN (File Reference Number) 作为唯一标识，需要改用 `路径哈希 + 文件 ID (nFileIndexHigh/Low)` 作为数据库主键，但这在文件移动时会丢失关联。

## 4. 解决方案建议
尽管技术上可以实现降权启动，但对于 ArcMeta 这种定位为“极致性能文件管理器”的应用，降权会导致其性能退化至与 Windows 自带资源管理器无异。

**建议方案：**
1.  **保持清单要求**：为了保证 MFT 核心功能，默认仍建议以管理员身份运行。
2.  **增加可选降权**：修改 `ArcMeta.manifest`，将 `requireAdministrator` 改为 `highestAvailable`。这样如果用户手动取消提权，程序可以进入“常规模式”。
3.  **UI 引导**：在普通权限运行时，在状态栏显示“普通权限：MFT/USN 监控已禁用，性能受限”。

## 5. 风险警告
- **索引速度**：常规递归扫描 100 万个文件可能需要几分钟，而 MFT 扫描仅需几秒。
- **实时性**：普通监控在大量文件变动时容易溢出缓冲区，不如 USN 日志稳定。
