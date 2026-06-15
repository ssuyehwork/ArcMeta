# 架构分析与修改方案 - ContentPanel 重命名逻辑缺陷修复（Delegate 与 Model 责任解耦方案）

## 1. 现状剖析与缺陷诊断

### 1.1 业务流程映射
在 `ContentPanel` 中，单文件重命名的执行流如下：
1. **触发层**：用户编辑文件名完成。
2. **委托层 (`GridItemDelegate::setModelData`)**：获取编辑器文本，**抢先执行**物理重命名，随后调用 `model->setData`。
3. **模型层 (`FerrexVirtualDbModel::setData`)**：再次尝试物理重命名，并更新内部缓存路径。

### 1.2 核心缺陷（Bug）根因
当前架构存在**“责任重叠”**导致的逻辑冲突：
- **冲突点**：Delegate 已经通过 `QFile::rename` 将文件移走，导致随后 Model 层的 `ShellHelper::renameItem` 因找不到源路径而必然失败。
- **后果**：Model 层的失败导致其跳过了 `mutableRecord.path = nativeNewPath` 这一步，使得模型内部持有的路径仍为“旧路径”，造成内存与磁盘状态的严重脱节。

---

## 2. 解决方案建议（已达成共识）

### 2.1 遵循“单一事实来源”原则
将物理重命名的操作权限彻底收拢至 **Model（模型层）**。Delegate 仅作为 UI 数据采集器，不应干预磁盘物理状态。

### 2.2 委托层 (Delegate) 优化
移除 `GridItemDelegate::setModelData` 中的 `QFile::rename` 逻辑，改为纯粹的 `setData` 调用。Delegate 仅负责将新值传递给模型，并在模型返回成功后处理元数据同步及 UI 刷新。

### 2.3 模型层 (Model) 职责强化
强化 `FerrexVirtualDbModel::setData` 中的重命名逻辑：
- 负责物理改名 (`ShellHelper::renameItem`)。
- 负责撤销栈记录 (`UndoManager`)。
- 负责内存缓存更新（`mutableRecord.path`）。
- 负责发送 `dataChanged` 信号。

---

## 3. 预期改进效果
- **逻辑闭环**：消除“双重改名”导致的静默失败，确保 Model 内部路径永远与磁盘同步。
- **架构解耦**：Delegate 与 Model 职责清晰，Delegate 负责 UI 交互，Model 负责数据持久化。
- **鲁棒性提升**：由于物理操作统一归口，系统能更准确地捕捉并向用户报告（如文件被占用等）重命名失败的具体原因。

## 4. 结论
通过本次架构解耦，将彻底解决 `ContentPanel` 在重命名时由于逻辑冲突导致的“数据不同步”隐患。
