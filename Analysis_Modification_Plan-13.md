# 5万规模数据扫描崩溃排查与 WER Dump 机制分析 (Analysis_Modification_Plan-13)

## 1. 崩溃现场与 WER Dump 生成判定
针对用户提到的“扫描 5 万条数据即将完成时闪退”的现象，关于 Windows 错误报告 (WER) 的排查如下：

- **Dump 生成判定**: 
  - 本应用**未集成**自定义崩溃捕获库（如 Breakpad/Crashpad），也**未安装** `SetUnhandledExceptionFilter`。因此，程序完全依赖 Windows 操作系统的默认处理。
  - **路径分析**: `C:\ProgramData\Microsoft\Windows\WER\ReportArchive` 确实是 Windows 存储崩溃报告的默认目录。如果系统配置了“收集本地转储”，那么在该路径的子文件夹中会包含 `Dump.dmp` 文件。
  - **限制**: 默认情况下，Windows 可能仅生成微型转储 (Minidump) 或仅记录事件。若要强制生成详细 Dump，通常需要在注册表 `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps` 下为 `ArcMeta.exe` 配置 `DumpType`。

## 2. 崩溃原因深度剖析 (架构缺陷)
通过对 `ContentPanel.cpp` 和 `MetadataManager.cpp` 的代码审计，发现在处理 5 万条大规模数据时存在两个致命风险点：

### 2.1 UI 信号风暴 (Signal Storm)
- **触发链路**: `ActionAddToCategory` (后台线程递归) -> `MetadataManager::registerItem` -> `notifyUI(PathUpdate)` -> `emit metaChanged(path)`。
- **后果**: 
  - 扫描 5 万个文件意味着在极短时间内向主线程发射了 **50,000 个**异步信号。
  - 主线程的事件循环（Event Loop）会被这 5 万个信号瞬间塞满。
  - 每一条信号都会触发 `MainWindow` 里的 Lambda 回调，进而调用 `ContentPanel::updateItemMetadata`，导致模型进行 5 万次索引查找和视觉刷新。
- **崩溃原理**: 这种超高频的信号积压极易引发资源耗尽（GDI 句柄或内存碎片），或者在模型更新过程中触发死锁/竞争，最终表现为“突然消失”（闪退）。

### 2.2 内存对账压力
- **逻辑**: `notifyCategoryCountChanged` 内部会调用 `CategoryRepo::fullRecount()`。
- **风险**: 如果在高频扫描期间频繁触发全量重新计数，SQLite 的并发性能将达到极限，且大量临时对象会在内存中积压，可能导致 OOM (Out of Memory)。

## 3. 建议的架构加固方案 (决绝方案)

为了支持 10 万+ 级的数据稳健处理，必须进行以下调整：

1. **静默扫描机制 (Silent Mode)**:
   - 在执行“扫描入库”期间，**暂时挂起** `MetadataManager` 的 `metaChanged` 信号发射。
   - 扫描线程只负责入库（物理写入），不负责即时通知 UI。

2. **批量 UI 刷新**:
   - 扫描完成后，发送一个单次的 `RefreshLevel::FullRebuild`（即 `__RELOAD_ALL__`）信号。
   - 让 UI 执行一次性重载，而不是 5 万次局部刷新。

3. **递归深度优化**:
   - 目前使用标准 `std::function` 递归扫描。对于极深目录树，建议改用 `QDirIterator` 迭代器模式以节省栈空间。

## 4. 结论
目前的崩溃是典型的**“同步过载”**导致的系统假死继而引发的闪退。WER 目录中若无 Dump，很可能是因为资源耗尽导致的非正常进程退出。

**建议**: 方案应立即转向“批量入库 + 单次刷新”模式。
