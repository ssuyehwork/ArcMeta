# 数据库文件名添加盘符后缀及自适应重命名方案 —— Analysis_Modification_Plan-97.md

## 1. 任务背景
当前系统生成的数据库文件命名格式为 `Arcmeta_<VolumeSerial>.db`。在最新版本中，虽然 `DatabaseManager` 引入了纠偏算法，但在程序启动加载旧数据库时，由于缺乏对物理挂载状态的感知，未能成功触发自动重命名。用户期望在启动时能够自动将旧库更名为 `Arcmeta_SERIAL_LETTER.db` 格式，且在盘符漂移时能自动纠正。

## 2. 问题定位
- **涉及组件**：`DatabaseManager` (核心加载)、`MetadataManager` (初始化逻辑)。
- **根因分析**：
    1. `MetadataManager::initFromScchMode` 仅遍历 `.arcmeta` 文件夹内的文件，由于此时无法感知文件对应的物理盘符，调用 `getMemoryDb` 时未传参，导致纠偏逻辑失效。
    2. `DatabaseManager::init` 接口目前为空实现，未利用 Windows API 主动探测物理卷。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 创建数据库时添加一个盘符到后面 | 接口支持并优先生成 `Arcmeta_SERIAL_LETTER.db` | ✅ |
| 2    | 数据库名称也应自动重命名 | 增加物理挂载探测，主动触发纠偏重命名 | ✅ |
| 3    | 禁止删除/覆盖 | 冲突文件重命名为 `_无效.db` 以保留 | ✅ |

## 4. 详细解决方案

### 4.1 `DatabaseManager` 核心增强
1. **死锁预防重构**：
   - 提取 `getMemoryDbInternal(...)` 私有方法（不加锁），供内部 `init` 和外部 `getMemoryDb` 调用，防止 `lock_guard` 递归死锁。
2. **主动探测逻辑 (纠偏核心)**：
   - 在 `DatabaseManager::init()` 中，不再保持空实现。
   - 遍历 `QDir::drives()`，对每个 NTFS 驱动器：
     - 获取序列号 `volSerial` 和当前盘符首字母 `letter`。
     - **主动调用** `getMemoryDbInternal(volSerial, letter)`。
     - **预期效果**：此操作会发现 `.arcmeta` 下存在的旧库（如 `_SERIAL.db`），并根据当前探测到的物理 `letter` 执行物理重命名。

### 4.2 `MetadataManager` 加载顺序优化
1. **阶段一：物理感知加载**：
   - 调用 `DatabaseManager::instance().init()`。此时所有当前在线的物理盘对应的数据库都会被加载且自动纠正名称后缀。
2. **阶段二：文件兜底加载**：
   - 遍历 `.arcmeta` 目录下的 `Arcmeta_*.db` 文件。
   - 如果发现某些数据库文件（可能来自当前未挂载的硬盘）尚未被阶段一加载，则执行静默加载（不传盘符参数），确保离线数据的可见性。

### 4.3 冲突处理细化
- **数据安全红线**：若重命名目标路径已存在，严禁覆盖。
- **备份策略**：将旧文件更名为 `Arcmeta_SERIAL_无效_TIMESTAMP.db`，并在日志中明确记录“检测到命名冲突，历史数据已备份”。

## 5. 修改边界声明【红线】
**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.h / .cpp`：重构锁机制，实现物理驱动器主动探测。
- [ ] `src/meta/MetadataManager.cpp`：优化 `initFromScchMode` 的两阶段加载顺序。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MetadataManager` 内存缓存的 Key 映射逻辑。
- [ ] 禁止在非 Windows 环境下调用特定卷序列号获取 API。

## 6. 实现准则与预警【核心】
1. **权限预警**：物理驱动器探测需依赖 `GetVolumeInformationW`，确保程序具备足够的访问权限。
2. **性能预警**：`init` 阶段的重命名操作应在加载 SQLite 句柄之前完成。若文件被占用（如被杀毒软件锁定），必须捕获 `QFile::rename` 的失败返回值，并平滑退回到“加载原始文件”模式。
3. **参数清洗**：`driveLetter` 统一通过 `QString::at(0).toUpper()` 处理，防止路径符注入风险。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 数据库管理 | DatabaseManager 负责分库路径生成与同步 | ✅ |
| 数据安全 | 禁止静默删除或覆盖用户数据库文件 | ✅ |

## 8. 待确认事项
- 在 `init` 阶段，如果检测到多个驱动器拥有相同的序列号（虚拟磁盘常见），方案将仅对第一个探测到的驱动器执行重命名纠偏。
