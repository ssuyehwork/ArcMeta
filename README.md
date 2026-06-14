# 备份备注

**备份时间**：2026-06-14 15:31:04  
**备份目录**：Buk_20260614_153103  

---

根据用户的最新需求，我修改了 `MetadataManager.cpp`，将元数据保存后的通知机制从“局部刷新”升级为“全量刷新”：

1.  **修改范围**：涉及 `setColor`, `setTags`, `setNote`, `setURL`, `setRating` 和 `renameItem` 六个核心函数。
2.  **变更逻辑**：移除原有的 `notifyUI(RefreshLevel::PathUpdate, ...)` 信号发射，统一改为调用 `notifyFullUIRebuild()`。
3.  **最终效果**：在元数据面板（MetaPanel）修改并保存任何属性（星级、颜色、标签、备注、链接、文件名）后，主窗口都会接收到全量重建信号，进而驱动内容容器（ContentPanel）执行 `refreshAll()`。这确保了内容容器中的文件状态始终与底层数据保持绝对同步，彻底消除了“显示旧状态”的疑惑。
