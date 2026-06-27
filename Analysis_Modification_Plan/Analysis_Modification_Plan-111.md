# 托管库入库逻辑与 USN 监听定点修复 —— Analysis_Modification_Plan-111.md

## 1. 任务背景
针对当前版本中 `AutoImportManager` 的严重逻辑偏差进行定点修复。主要解决路径格式不匹配导致的监听失效、任务执行流未对齐全局规范、以及缺失实质性暂停与单层级监听约束的问题。

## 2. 问题定位与深度推理

### 2.1 路径匹配死路 (Slash Inconsistency)
- **分析**：`onEntryAdded` 构造的 `fullPath` 使用 Windows 原生反斜杠（如 `D:\...`），而 `getManagedFolderAbsolutePath` 通过 Qt API 获取的路径常含正斜杠（如 `D:/...`）。
- **后果**：`_wcsnicmp` 前缀匹配永远失败，导致 `checkAndGetManagedPath` 恒返回 `false`，USN 变动无法记录到数据库。
- **修复**：统一调用 `QDir::toNativeSeparators` 规范化为反斜杠后比较。

### 2.2 执行流违规 (Redline Violation)
- **分析**：`startTask` 目前错误地调用了 `MetadataManager::registerItem`。
- **后果**：绕过了 `ImportHelper` 的分类逻辑、事务处理及 UI 进度反馈。
- **修复**：重构为从 `pending_imports` 提取路径列表，批量投递给 `ImportHelper::importPaths`。

### 2.3 状态机虚设 (Logic Void)
- **分析**：`pauseTask` 函数体为空。
- **后果**：UI 上的“暂停”按钮仅改变了图标颜色，后台任务仍在后台线程中盲目运行。
- **修复**：引入线程安全的暂停标志位，并在任务循环中实现中断检测。

### 2.4 监听洪流 (Monitoring Scope)
- **分析**：目前的 USN 监听捕获了托管文件夹下的所有层级。
- **后果**：缓冲区条目过多，且与 `ImportHelper` 的递归扫描逻辑重叠。
- **修复**：落实“单层级监听”，仅处理托管文件夹的直属子项（第一层级）。

---

## 3. 强制对照表

| 编号 | 用户指令 / 我的理解 | 方案对应点 | 是否一致 |
|------|-------------------|------------|----------|
| 1    | 统一斜杠为反斜杠后比较 | 在 `checkAndGetManagedPath` 入口执行规范化 | ✅ |
| 2    | startTask 批量调用 ImportHelper::importPaths | 重构任务循环，移除 `registerItem` | ✅ |
| 3    | 实现 m_isPaused 标志位中断循环 | 在 `AutoImportManager` 类中增加原子标志位 | ✅ |
| 4    | USN Journal 只监听第一层级（直接子项） | 在 `onEntryAdded/Removed` 增加层级校验逻辑 | ✅ |

---

## 4. 详细解决方案

### 4.1 监听层：单层级验证
- **onEntryAdded**：
    - 获取 `fullPath`。
    - 提取其父目录 `parentPath = QFileInfo(fullPath).absolutePath()`。
    - 构建默认托管文件夹路径 `managedFolderDefault`。
    - 比较 `toNativeSeparators(parentPath)` 与 `toNativeSeparators(managedFolderDefault)`，非直属项直接 return。
- **onEntryRemoved**：
    - 由于物理文件已消失，无法直接获取 path。
    - 逻辑：尝试在 `pending_imports` 表中按 `frn` 查询。
    - 若记录存在，说明该项曾作为直属项入库，执行 `status = -1` 更新。
    - 若记录不存在，说明该项属于深层子项或非托管项，忽略变动。

### 4.2 执行层：规范化消费
- **startTask**：
    1. 修改类成员增加 `std::atomic<bool> m_isPaused`。
    2. 收集 `status = 1` 的路径。
    3. 在循环处理每一项前，检查 `m_isPaused`。
    4. 批量入库：调用 `ImportHelper::importPaths`。由于该函数内部涉及 UI (进度条)，必须通过 `QMetaObject::invokeMethod` 确保在 GUI 线程执行。
- **pauseTask**：设置 `m_isPaused = true`。

---

## 5. 修改边界声明【红线】

**允许修改：**
- [x] `src/core/AutoImportManager.h` (成员变量定义)
- [x] `src/core/AutoImportManager.cpp` (核心逻辑修复)

**明确禁止修改：**
- [ ] 任何其他文件及底层算法。

---

## 6. 实现准则与预警
- **Windows 路径陷阱**：在处理 `D:/` 与 `D:` 等盘符前缀时，需确保 `toNativeSeparators` 后的比较逻辑严密。
- **GUI 线程切换**：必须处理好 `QtConcurrent` 线程与 GUI 线程的同步。
