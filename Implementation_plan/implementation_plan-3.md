# 自动入库感知链路重构 —— implementation_plan-3.md

## 1. 任务背景
解决手动移入（收揽）文件/文件夹至托管库目录（`ArcMeta.Library_盘符`）后，系统无法自动登记、入库且 UI 无反馈的问题。该问题涉及单文件移入失败、批量移入丢失感知以及状态位流转错误。

## 2. 问题定位

### 2.1 盘符索引错位（致命伤）
- **精确位置**：`AutoImportManager::onEntryAdded` (src/core/AutoImportManager.cpp)。
- **根因分析**：`MftReader` 在发射信号时，`key` 的高 16 位存的是其内部 `m_drive_list` 的物理盘符索引。而 `AutoImportManager` 接收信号后，错误地使用 `QDir::drives()` 系统列表索引来还原路径。
- **后果**：路径还原指向了错误的磁盘，导致物理准入判定（`isPathInManagedLibrary`）静默失败（对应用户描述："移入 1 个文件也没有注册登记"）。

### 2.2 批量信号截断
- **精确位置**：`MftReader::updateEntriesFromUsnBatch` (src/mft/MftReader.cpp)。
- **根因分析**：代码设定当变动超过 50 个文件时停止发射单体 `entryAdded` 信号。由于 `AutoImportManager` 缺乏对批量信号的监听，导致大批量移入时感知完全丢失。

### 2.3 递归登记缺失
- **现象描述**：移入文件夹后，仅文件夹本身被登记，子项无动作。
- **根因分析**：`AutoImportManager` 在处理新增文件夹时，未执行深度递归扫描并提交登记。

### 2.4 状态机实现违规
- **精确位置**：`MetadataManager::registerItem` (src/meta/MetadataManager.cpp:360)。
- **根因分析**：该函数在登记阶段暴力将 `ingestionStatus` 设为 `1`。
- **后果**：违反了“登记(0) -> 入库(1)”的状态流转规范，导致元数据未提取完成时就错误显示绿勾（对应用户疑问："元数据入库唯一途径是什么方式？"）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 移入 1 个文件也没有注册登记并入库 | 修复 `AutoImportManager` 盘符解析逻辑，对齐索引标准 | ✅ |
| 2    | 元数据入库唯一途径是什么方式？ | 明确 `metadata` 表及状态位流转，修复 `registerItem` 违规设值 | ✅ |
| 3    | 批量移入感知丢失 | 新增 `entriesBatchAdded` 信号监听并适配流量整形 | ✅ |
| 4    | 自动感知物理变动 | 修复 USN 信号监听与路径还原链路 | ✅ |

## 4. 详细解决方案

### 4.1 统一盘符映射标准
- 在 `MetadataManager` 中封装 `getDriveLetterByMftIndex(int index)` 接口，直接访问 `MftReader` 的索引列表。
- 修改 `AutoImportManager`，在接收 `entryAdded` 信号时调用上述接口还原路径，废除对 `QDir::drives()` 的依赖。

### 4.2 适配批量信号链路
- 在 `MftReader` 触发批量截断处，发射 `entriesBatchAdded(int driveIdx, QList<uint64_t> frns)` 信号。
- `AutoImportManager` 监听该信号，将批量 FRN 转换为物理路径并执行批量准入判定。

### 4.3 异步递归登记
- 修正 `AutoImportManager::registerPath`。
- 若路径为文件夹，启动异步线程通过 `QDirIterator` 遍历所有子项。
- 调用 `MetadataManager::registerItemsAsync` 对所有子项执行初始登记。

### 4.4 状态位逻辑回归
- 修改 `MetadataManager::registerItem`：初始 `ingestionStatus` 必须设为 `0`。
- 仅在 `MetadataManager` 完成颜色、尺寸等物理解析后，由解析回调将状态更新为 `1`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/core/AutoImportManager.cpp / .h`：重构信号感知与路径还原逻辑。
- [ ] `src/mft/MftReader.cpp / .h`：新增批量变动信号接口。
- [ ] `src/meta/MetadataManager.cpp / .h`：修正登记初值逻辑，提供统盘符转换服务。

**明确禁止越界修改的范围：**
- [ ] 禁止修改数据库 `metadata` 表的 Schema 定义。
- [ ] 禁止修改 `ThumbnailDelegate` 的 UI 渲染逻辑。

## 6. 实现准则与预警
1. **类型匹配**：`MftReader` 的索引是基于磁盘物理挂载顺序的，必须确保 `MetadataManager` 缓存的顺序与其完全同步，防止在热插拔场景下出现偏移。
2. **性能保护**：递归登记 10 万+级别的文件时，必须分批次（每批 1000 个）提交数据库事务，避免 SQLITE_BUSY 错误。
3. **头文件规范**：`AutoImportManager` 需显式引入 `MetadataManager.h` 以支持批量登记接口调用。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 入库状态定义 | 登记(0)不等于入库(1) | ✅（已在 4.4 节修正） |
| 自动感知逻辑 | AutoImportManager 驱动入库 | ✅（修复了感知链路致命 Bug） |
| 物理准入 | ArcMeta.Library_ 路径校验 | ✅（修复了路径解析依赖） |

## 8. 待确认事项
- **USN 溢出处理**：在极高频 IO 下，USN 日志可能发生溢出导致信号丢失，本方案建议在感知失效时通过 `reScanDrive` 作为保底，是否需要在此方案中实现保底机制？
