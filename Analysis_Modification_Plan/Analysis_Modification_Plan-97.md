# 盘符管理栏右键交互与托管文件夹自动化方案 —— Analysis_Modification_Plan-97.md

## 1. 任务背景
在已有的盘符管理栏（DriveBar）基础上，深度集成“托管文件夹”管理功能。通过右键菜单实现 ArcMeta.FERREX 文件夹的快捷创建与访问，并利用 USN Journal 基础设施实现该文件夹内容的自动入库与出库。同时，引入盘符优先级调度机制，支持任务插队及 UI 状态实时反馈（动画）。

## 2. 问题定位
- **涉及组件**：
    - `MainWindow` (UI 布局与右键菜单)
    - `AutoImportManager` (USN 监听分发与自动入库触发)
    - `ImportHelper` (入库执行引擎)
    - `MetadataManager` (出库/失效执行引擎)
- **核心挑战**：
    1. **任务串行化**：需要建立一套基于盘符优先级的全局调度器，确保大批量导入任务不发生 IO 冲突。
    2. **UI 动态重排**：盘符按钮的位置需随优先级动态调整（最左侧最高优先级）。
    3. **非阻塞动画**：盘符按钮需在不影响点击的前提下，绘制旋转加载图标。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 右键菜单包含“创建/打开托管文件夹” | `MainWindow` 实现动态右键菜单逻辑 | ✅ |
| 2    | 每个盘符只允许存在一个 ArcMeta.FERREX | 创建前执行物理存在性校验 | ✅ |
| 3    | USN 监听 ArcMeta.FERREX 内变化 | 扩展 `AutoImportManager` 的匹配算法 | ✅ |
| 4    | 优先任务：盘符插队到最左侧 | 动态调整 `m_driveBarLayout` 控件顺序 | ✅ |
| 5    | 任务串行执行，完成后恢复默认顺序 | 实现 `TaskScheduler` 调度逻辑 | ✅ |
| 6    | 盘符按钮显示转圈动画 | 复用 `LoadingWindow` 的旋转 SVG 绘制逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 盘符按钮增强 (`DriveButton`)
1. **自定义控件**：创建一个内部类或独立类 `DriveButton` 继承自 `QPushButton`。
2. **绘制逻辑**：
   - 成员：`m_rotationAngle`, `m_animationTimer`, `m_isLoading`。
   - `paintEvent`：若 `m_isLoading` 为真，在文字左侧或右侧绘制旋转的 `refresh` 图标（复用 `QSvgRenderer`）。
3. **右键支持**：设置 `setContextMenuPolicy(Qt::CustomContextMenu)`。

### 4.2 UI 交互中心 (`MainWindow`)
1. **右键菜单响应**：
   - 动态判定路径：`Root:\ArcMeta.FERREX`。
   - 选项一：`createManagedFolder()` -> `QDir().mkpath(...)`。
   - 选项二：`openManagedFolder()` -> `QDesktopServices::openUrl(...)`。
   - 选项三：`setPriorityTask()` -> 见下文。
2. **优先级调度与重排**：
   - 维护 `m_priorityDrives` (已手动设为优先的盘符) 和 `m_activeTasks` (当前有任务的盘符)。
   - **重排逻辑**：清空 `m_driveBarLayout`，按照 `[优先级盘符] + [普通在线盘符]` 顺序重新插入。
   - **任务关联**：当某个盘符的任务开始时，调用 `btn->setLoading(true)`。

### 4.3 自动化逻辑层 (`AutoImportManager` 扩展)
1. **匹配算法升级**：
   - 原有逻辑：匹配 `AppConfig` 托管路径。
   - 新增逻辑：任何路径若包含 `\ArcMeta.FERREX\` 且位于根目录，均视为托管。
2. **删除/移出监听**：
   - 连接 `MftReader::entryRemoved` 信号。
   - 若被删除路径位于托管文件夹内，调用 `MetadataManager::removeMetadataSync()` 或 `setInvalid()`。
3. **任务调度派发**：
   - 捕获 USN 变化后，不直接调用 `ImportHelper`，而是加入 `TaskScheduler` 队列。

### 4.4 全局任务调度器 (`TaskScheduler`)
1. **工作模式**：
   - 串行队列（FIFO + Priority）。
   - 当前任务完成（通过监听 `ImportHelper` 相关完成信号或 Future）后，自动拉取队列中下一个最高优先级的盘符执行。
2. **UI 联动**：
   - 任务开始：通知 `MainWindow` 对应按钮开始动画。
   - 任务结束：通知 `MainWindow` 对应按钮停止动画；若全队列空，触发盘符顺序复位。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.h / .cpp`：UI 集成与菜单交互。
- [ ] `src/core/AutoImportManager.h / .cpp`：监听逻辑扩展与调度派发。
- [ ] `src/util/ImportHelper.cpp`：微调，支持静默后台导入模式。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MftReader` 的 SoA 数组结构或查询算法。
- [ ] 禁止修改数据库 `metadata` 表的 Schema。

## 6. 实现准则与预警【核心】
1. **复用原则**：必须通过 `UiHelper::getIcon("refresh", ...)` 获取动画图标。
2. **IO 冲突预警**：USN 记录可能极多，调度器必须具备“任务合并（Batching）”能力，避免对同一盘符发起多个重叠导入任务。
3. **权限检查**：在创建 ArcMeta.FERREX 失败时，需通过 `ToolTipOverlay` 提示用户权限不足。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 托管文件夹配置 | 使用卷序列号作为 Key 存储相对路径 | ✅ |
| 动画性能 | 杜绝大面积重绘，旋转逻辑需精准控制区域 | ✅ |
| 菜单样式 | 统一使用 applyMenuStyle | ✅ |

## 8. 待确认事项
- **动画复用确认**：已确认现有代码中 `LoadingWindow` 具备成熟的 SVG 旋转逻辑，将直接移植其 `QTimer` 方案。
- **调度粒度**：后台同步任务是否需要弹出 `BatchProgressDialog`？（建议：由于是自动同步，优先采用静默模式，仅在 DriveBar 按钮显示动画反馈）。
- **默认排序**：任务全部归零后的“默认顺序”将统一采用盘符字母升序。
