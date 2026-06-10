# 撤销 (Undo) 与 恢复 (Redo) 双栈架构设计方案 (Analysis_Modification_Plan-9)

## 1. 核心目标
在 `Plan-8` 撤销机制的基础上，引入“重做/恢复 (Redo)”逻辑，通过双栈管理确保用户可以在操作历史中自由穿梭，解决误撤销问题。

## 2. 架构深度推理

### 2.1 双栈模型 (Undo & Redo Stacks)
- **Undo Stack**: 存储已执行的操作。执行 `Undo` 时，弹出栈顶 Command，执行其 `undo()`，并将其压入 Redo Stack。
- **Redo Stack**: 存储已撤销的操作。执行 `Redo` 时，弹出栈顶 Command，执行其 `redo()`（或重新执行 `execute()`），并将其压回 Undo Stack。

### 2.2 逻辑一致性约束 (Consistency Invariants)
- **破坏重做链**：如果用户在撤销了几步后，执行了一个**全新的操作**，系统必须立即清空 Redo Stack。
    - *理由*：新操作可能改变了重做链中后续指令的前提条件（如删除了重做链中即将重命名的文件），清空 Redo Stack 是保障数据一致性的行业标准做法。

### 2.3 指令接口扩展
```cpp
class ActionCommand {
public:
    virtual void execute() = 0; // 首次执行，捕获快照
    virtual void undo() = 0;    // 回滚至快照状态
    virtual void redo() { execute(); } // 恢复执行，通常可复用逻辑，但需标记不重复捕获快照
};
```

## 3. 实现难点与对策

### 3.1 物理 IO 的幂等性
对于 `MoveCommand` 的 Redo 操作，如果目标文件已因其他原因再次发生变动，Redo 需要具备“前置校验”能力，若环境不满足执行条件，应友好提示并停止 Redo。

### 3.2 UI 状态同步
撤销或恢复后，`ContentPanel` 和 `CategoryPanel` 必须通过 `MetadataManager::notifyUI` 接收信号并局部刷新，确保视图中的星级、路径等信息与底层数据模型同步。

## 4. 结论
通过升级至“双栈命令模式”，系统不仅具备了纠错能力（Undo），还具备了补偿能力（Redo）。这种对称架构是专业级文件管理器的核心基石，能够显著提升用户对复杂批量操作的可控感。
