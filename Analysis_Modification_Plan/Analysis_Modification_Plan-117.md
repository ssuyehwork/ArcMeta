# USN 实时感知链路全口径修复与调试增强 —— Analysis_Modification_Plan-117.md

## 1. 任务背景
当前版本的 USN 感知仅支持“单向准入”，即只能发现新入库的项目，无法感知项目的删除或移出。用户要求彻底修复该链路（对应用户原话：“我期望进行彻底修复”），确保新增和删减均能实时同步到数据库，并添加详尽的日志以便追踪定位（对应用户原话：“添加调试日志以便追踪并定位问题的所在”）。

## 2. 问题定位
*   **UsnWatcher.cpp**：
    *   缺少对 `USN_REASON_RENAME_OLD_NAME` 的解析，导致移动操作的“起点”丢失。
    *   缺乏关键分片处理和记录处理的调试日志（导致无法“定位问题的所在”）。
*   **MftReader.cpp**：
    *   在 `removeEntryByFrn` 中物理移除索引前，未充分暴露被删除项的路径信息。
*   **AutoImportManager.cpp**：
    *   未订阅 `entryRemoved` 信号（对应用户原话：“删减都必须感知到”）。
    *   `onEntryUpdated` 逻辑中仅检查“新路径”是否在库内，若项目移出库外，则直接跳过，未执行数据库注销（导致无法感知“移出”）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 我期望进行彻底修复 | 4.1-4.3 补全全口径感知链路 | ✅ |
| 2    | 添加调试日志以便追踪并定位问题的所在 | 4.1, 4.3 注入关键路径追踪日志 | ✅ |
| 3    | 无论是新增或删减都必须感知到 | 4.3 增加删除信号监听与移出逻辑判定 | ✅ |

## 4. 详细解决方案

### 4.1 UsnWatcher 信号捕获与日志增强
修改 `src/mft/UsnWatcher.cpp`：
1.  在 `run()` 的 ReasonMask 中显式包含 `USN_REASON_RENAME_OLD_NAME`。
2.  在批量处理循环起始处添加分片大小日志（对应用户原话：“追踪并定位问题的所在”）：
    `qDebug() << "[USN_TRACE] 正在处理批次记录，数量:" << updateBatch.size();`
3.  在解析每一条记录时打印详细上下文：
    `qDebug() << "[USN_TRACE] 捕获变更信号: FRN=" << frn << " 原因掩码=" << hex << reason;`

### 4.2 MftReader 删除链路补全
修改 `src/mft/MftReader.cpp` 中的 `removeEntryByFrn`：
1.  在将 `m_frns[idx]` 置 0 之前，利用 `getPathFastInternal` 获取该项在内存中的最后已知路径（对应用户原话：“删减都必须感知到”）。
2.  添加删除追踪日志：
    `qDebug() << "[MFT_TRACE] 物理索引项已移除: FRN=" << frn << " 路径=" << QString::fromStdWString(path);`

### 4.3 AutoImportManager 业务逻辑闭环
修改 `src/core/AutoImportManager.cpp`：
1.  **信号连接**：在 `startListening` 中增加对 `MftReader::entryRemoved` 的订阅（对应用户原话：“彻底修复”）。
    `connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);`
2.  **实现 `onEntryRemoved(uint64_t key)`**：
    *   通过 FID 反查 `MetadataManager` 缓存。
    *   判定被删除路径是否在库内（对应用户原话：“删减都必须感知到”）。如果是，调用 `MetadataManager::instance().deletePermanently`。
    *   添加日志：`qDebug() << "[AIM_TRACE] 感知到物理删除，执行数据库同步注销:" << key;`
3.  **重构 `onEntryUpdated(uint64_t key)`**：
    *   获取新路径。
    *   **移出判定**（对应用户原话：“彻底修复”）：若新路径不在库内，但该 FID 在数据库中有“已登记”记录，则判定为移出。
    *   执行 `MetadataManager::instance().deletePermanently`。
    *   添加日志：`qDebug() << "[AIM_TRACE] 项目已移出托管库，同步注销数据库记录: FRN=" << key;`

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块：`src/mft/` (UsnWatcher, MftReader)
- [ ] 模块：`src/core/` (AutoImportManager)
- [ ] 逻辑：USN 日志流过滤、重命名 OLD_NAME 识别、入库/出库状态机判定。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `Sqlite` 预读机制。
- [ ] 禁止修改 `MetadataManager` 的读写锁底层实现。

## 6. 实现准则与预警【核心】
1.  **日志前缀**：所有追踪日志必须包含 `[TRACE]` 关键字（对应用户原话：“添加调试日志”）。
2.  **锁安全**：在 `onEntryRemoved` 触发数据库注销时，必须确保不持有 MFT 端的读锁，防止与 `MetadataManager` 的持久化写锁发生死锁。
3.  **路径一致性**：所有路径判定必须经过 `MetadataManager::normalizePath` 处理。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 日志规范 | 不允许使用英文答复用户（日志内容除外） | ✅ |
| 入库授权 | 仅允许 AutoImportManager 标记 authorized=true | ✅ |
| FID 协议 | 遵循 Plan-113 架构，废除 pending_imports | ✅ |

## 8. 待确认事项
*   目前删除操作会导致 `MftReader` 索引立即失效，反查路径的逻辑必须置于 `MftReader::removeEntryByFrn` 的**物理删除动作之前**。
