# 备份备注

**备份时间**：2026-08-10 19:52:51  
**备份目录**：Buk_20260810_195247  

---

本次修改对可撤销操作反馈浮窗进行重构并轨，使其与全局 Ctrl+Z (UndoManager) 完全合并：
1. 修改了 UndoToastOverlay，使得点击气泡上的“撤销”按钮时统一调用全局并轨的 UndoManager::instance().undo()。
2. 重构了 OperationSnapshotEngine 的 executeWithSnapshot，在写操作成功后自动将 beforeState 快照与 undo 动作封装为 GeneralSnapshotUndoCommand 推送至 UndoManager，完美实现了气泡撤销与快捷键撤销的完全一致。
3. 修改了 LibraryAssetModel 的单项行内重命名，确保其成功执行时亦将 RenameCommand 推入全局 UndoManager，防止了撤销与物理、虚拟数据断裂的问题。
