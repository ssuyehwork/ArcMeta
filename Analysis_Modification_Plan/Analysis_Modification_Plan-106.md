# 内容面板加载性能与视觉闪烁修复 —— Analysis_Modification_Plan-106.md

## 1. 任务背景
在当前版本中，当用户切换目录、加载分类或查看系统分类（如回收站）时，内容区会出现明显的“闪白/闪黑”现象。这是因为 `ContentPanel` 在启动异步数据加载前，会先行调用 `m_model->clear()` 清空视图，导致在数据扫描的空窗期内视图显示为空。

## 2. 问题定位
- **模块**：`src/ui/ContentPanel.cpp`
- **函数**：`loadDirectory()`、`loadPaths()`、`loadCategory()`
- **根因分析**：
    - `m_model->clear()` 触发了 `beginResetModel()`，这要求 Qt 立即销毁视图中的所有条目渲染对象。
    - 后台扫描（物理磁盘 I/O 或数据库查询）耗时较长（对应用户原话：“扫描耗时（秒级）”）。
    - 在此期间，主线程 UI 因模型已清空而显示空白，直至扫描完成后的 `setRecords()` 再次触发 `beginResetModel()` 填充新数据。
    - 这种“销毁 -> 等待 -> 重建”的模式造成了不必要的视觉震荡。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 第一步：删除 loadDirectory() 中的 m_model->clear() 调用 | 物理移除该行代码 | ✅ |
| 2    | 第二步：后台扫描完成的回调里，直接调用 setRecords(allItems) | 维持原有 setRecords 调用 | ✅ |
| 3    | 同步修复 loadPaths() 和 loadCategory() | 同步移除其中的 clear 调用 | ✅ |
| 4    | 后台扫描期间保留旧数据继续显示 | 不再执行预清空逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 修改 `loadDirectory(const QString& path, bool recursive)`
- **操作**：移除函数中的 `m_model->clear();` 调用（对应用户原话：“第一步：删除 loadDirectory() 中的 m_model->clear() 调用”）。
- **逻辑**：不清空，保留旧数据继续显示（对应用户原话：“不清空，保留旧数据继续显示”）。
- **替换**：当后台扫描完成的回调里，直接调用 `setRecords(allItems)`（对应用户原话：“第二步：后台扫描完成的回调里，直接调用 setRecords(allItems)”）进行一次性数据替换。

### 4.2 修改 `loadCategory(int categoryId)`
- **操作**：移除其中的 `m_model->clear()` 调用（对应用户原话：“同步移除其中的 m_model->clear() 调用”）。
- **逻辑**：切换分类时旧内容将驻留直至新内容就绪，消除视觉空窗。

### 4.3 修改 `loadPaths(const QStringList& paths, int reqId)`
- **操作**：移除其中的 `m_model->clear()` 调用（对应用户原话：“同步移除其中的 m_model->clear() 调用”）。
- **注意**：函数开头的 `if (paths.isEmpty()) { ... m_model->clear(); ... }` 逻辑需**保留**。因为当目标路径列表确实为空时（如无结果状态），必须立即清空视图以反馈真实状态。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`
- [ ] 函数：`loadDirectory`、`loadPaths`、`loadCategory` 的内部清空逻辑。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `FerrexVirtualDbModel` 的 `setRecords` 或 `clear` 实现。
- [ ] 禁止修改 `search()` 函数的清空逻辑（除非用户后续明确要求）。
- [ ] 禁止修改 `m_loadRequestId` 的竞态保护机制。

## 6. 实现准则与预警【核心】

### 6.1 UI 考古与对齐 (Code Archaeology First)
- **现有案例**：本次修改属于对 `ContentPanel.cpp` 现有逻辑的定点修复，而非新建组件。
- **对齐标准**：修改方案严格对齐 `FerrexVirtualDbModel::setRecords` 的原子替换特性，确保与现有的 `m_loadRequestId` 竞态保护机制逻辑一致。

### 6.2 关键实现细节
1. **头文件依赖**：无需新增。
2. **原子性保证**：`FerrexVirtualDbModel::setRecords` 内部已封装 `beginResetModel`/`endResetModel`。由于其内部操作均为纯内存操作，数据替换耗时极短（对应用户原话：“数据替换耗时（毫秒级）”），用户几乎感知不到闪烁。
3. **竞态安全**：
    - 加载期间点击旧项：由于 `onDoubleClicked` 或 `onCustomContextMenuRequested` 依赖 `index.data()`，即使数据即将过期，操作仍会指向物理存在的路径，这符合“加载期间操作可接受”的设计预期。
    - 快速切换路径：若用户在扫描期间快速点击了新路径，`m_loadRequestId` 的递增机制会确保只有最后一次请求的回调能成功执行 `setRecords`，旧的扫描结果会被 `if (panelPtr->m_loadRequestId == reqId)` 拦截，不会出现数据串扰。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 刷新抑制 | 避免不必要的全量刷新信号 | ✅ 符合。减少了一次 `__RELOAD_ALL__` 等效的 Model Reset，实现局部平滑。 |
| 内容面板拖拽信号 | 确保拖拽与视图初始化规范 | ✅ 无冲突。本方案不触及拖拽逻辑。 |

## 8. 待确认事项（可选）
- 无。方案已完全对齐用户提供的技术路径。
