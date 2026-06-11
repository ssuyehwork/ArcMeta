# “扫描入库”功能可靠性与断电恢复分析 (Analysis_Modification_Plan-11)

## 1. 现状逻辑分析
“扫描入库”功能（ActionAddToCategory）采用递归逐个处理模式，核心链路如下：
- **触发路径**: `ContentPanel.cpp` -> `ActionAddToCategory` -> `QtConcurrent::run`。
- **处理原子性**: 每扫描到一个文件，立即调用 `MetadataManager::registerItem(path)`。
- **持久化策略**: `registerItem` 内部同步触发 `syncPhysicalMetadata`，最终执行 SQLite 的 `INSERT OR REPLACE INTO metadata`。

## 2. 断电安全性评估 (Power Outage Reliability)
- **数据一致性**: 由于采用逐个文件的 `INSERT OR REPLACE` 逻辑，**已经完成扫描并成功执行 SQL 的项目在数据库层是即时生效的**。
- **丢失风险窗口**: 系统采用“内存数据库 + 异步磁盘备份”架构。断电仅会丢失“内存已写入但尚未同步到物理 `.db` 文件”的数据（通常为最后 15-30 秒的扫描结果）。
- **结论**: 断电不会导致“必须重新开始”，大部分已扫描进度已锁定在数据库中。

## 3. 进度恢复与续传机制
目前虽无显式“恢复进度”UI，但底层逻辑具备天然的续传能力：
- **幂等性 (Idempotency)**: 二次执行扫描时，SQL 引擎发现 FID 已存在，仅执行 `UPDATE` 而非 `INSERT`，开销极低。
- **mtime 对账逻辑**: 系统在扫描时会比对磁盘文件的修改时间。若与数据库一致，将跳过耗时的视觉特征提取（颜色计算），实现“秒级快进”。这种物理特征比对是应对中断最有效的“续传”方式。

## 4. 优化建议 (加固方案)
为了将“靠谱”提升至“工业级稳健”，建议在未来的迭代中：
1. **分段强制落盘 (Segmented Flush)**: 
   - **逻辑**: 在大批量扫描期间（如 `ActionAddToCategory` 中），每处理 500 个项目或每扫描 30 秒，强制调用一次 `DatabaseManager::flushAll()`。
   - **目的**: 缩短内存数据库与磁盘文件的同步差，确保断电损失控制在极小范围内。
2. **显式事务包装**: 引入 `SqlTransaction` RAII 类，将子目录的扫描结果封装在小事务中，提升写入性能。
3. **状态标记**: 在扫描根目录添加一个 `is_indexing` 标记位。
