# 撤销 (Undo) 与快照机制架构设计方案 (Analysis_Modification_Plan-8)

## 1. 核心目标
建立一套基于“命令模式”的全局撤销系统，当用户执行误操作（如错误的批量重命名或文件迁移）时，能够通过 `Ctrl+Z` 快速恢复至操作前的状态。

## 2. 架构设计推理

### 2.1 命令模式 (Command Pattern)
- **基类定义**：创建 `ActionCommand` 抽象基类。
    - `execute()`: 执行业务逻辑并记录快照。
    - `undo()`: 根据快照回滚状态。
- **具体指令实现**：
    - `RenameCommand`: 记录 `old_path` 和 `new_path`。
    - `MoveCommand`: 记录源目录与目标目录。
    - `MetadataCommand`: 记录元数据变更前后的具体值（如星级从 3 变 5）。
    - `CategorizeCommand`: 记录 FID 与 CategoryID 的关联增减。

### 2.2 状态快照 (State Snapshot)
- **轻量化原则**：快照仅记录“差异”与“元数据”，不备份真实文件内容。
- **时机捕获**：在 `MetadataManager` 或 `ShellHelper` 执行物理修改前的“临界点”，自动提取当前状态并封装进 Command 对象。

### 2.3 撤销栈管理器 (Undo Stack Manager)
- **归属**：集成至 `CoreController`，作为全局单例。
- **存储**：维护一个 `std::deque<std::unique_ptr<ActionCommand>>`，设置最大容量（如 50 步）。
- **清理逻辑**：当执行“永久删除”时，清理所有受影响文件的历史快照，防止撤销导致非法路径访问。

## 3. 各模块接入方案

### 3.1 元数据层 (MetadataManager)
- 现有的 `setRating`, `setColor` 等函数需重构为支持“指令化”。
- 调用流程：`UI触发 -> 创建 MetadataCommand -> 推入撤销栈 -> 执行修改`。

### 3.2 物理层 (ShellHelper)
- 所有的 `QFile::rename` 或 `copyOrMoveItems` 必须封装进 `MoveCommand`。
- **难点处理**：物理位移的撤销涉及磁盘 I/O。若撤销时目标路径已被占用，需向用户弹出冲突提示。

## 4. 结论
通过引入“命令模式”与“状态快照”，我们可以将系统从“单向操作”进化为“双向可逆”架构。这不仅提升了容错率，也极大地增强了用户在大规模整理文件时的心理安全感。
