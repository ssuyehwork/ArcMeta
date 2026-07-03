# MainWindow 职责过载分析与解耦规划 —— Analysis_Modification_Plan-120.md

## 1. 任务背景
`MainWindow.cpp` 当前行数已达 1625 行，作为一个顶层 UI 容器，其代码体积与逻辑复杂度均已触碰“上帝对象” (God Object) 的红线。根据“MainWindow 职责红线”规约，需要对其职责进行审计，识别并列举不属于 UI 容器范畴的过载点。

## 2. 问题定位
通过对 `src/ui/MainWindow.cpp` 的静态审计，确认其存在严重的职责过载。该类不仅负责窗口布局，还深度介入了硬件监听、物理文件操作及业务数据管理。

### 2.1 核心过载点审计
1. **硬件消息捕获 (违规行号: 585-601)**：
    - `nativeEvent` 中直接拦截 Win32 `WM_DEVICECHANGE` 消息并触发磁盘对账。
    - **归类**：属于底层系统监听职责，应由 `CoreController` 或专职 `HardwareService` 承载。
2. **物理磁盘 IO 与目录探测 (违规行号: 1547, 1585-1595)**：
    - `initDriveBar` 内部使用 `QDir::drives()` 遍历物理驱动器并检查物理路径。
    - `onDriveButtonContextMenu` 内部直接调用 `QDir().mkpath()` 创建托管库。
    - **归类**：属于物理磁盘 IO 职责，应由 `ShellHelper` 或 `CoreController` 的业务逻辑层处理。
3. **业务实体构造与持久化逻辑 (违规行号: 1599-1616)**：
    - 在右键菜单槽函数中，直接构造 `Category` 结构体、计算 FRN，并调用 `CategoryRepo::add()`。
    - **归类**：属于业务领域逻辑，顶层 UI 不应知晓 `Category` 结构的内部细节及持久化动作。
4. **协议解析与路由中枢过重 (违规行号: 1243-1310)**：
    - `unifiedNavigateTo` 承载了全应用所有的协议分流逻辑（category://, system://, file://）。
    - **归类**：应由专职的 `RouteController` 处理，UI 仅需向控制器请求跳转。
5. **复杂交互逻辑碎片化 (违规行号: 669-722)**：
    - 大量的鼠标按下、移动事件处理自定义窗口缩放 (Resize) 和拖拽。
    - **归类**：虽属 UI，但建议封装入 `FramelessHelper` 或专门的事件过滤器以精简主类。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | MainWindow.cpp 是否存在职责过载？ | 确认存在严重过载（上帝对象） | ✅ |
| 2    | 存在哪些职责过载呢？ | 详细列举了硬件消息、磁盘 IO、业务持久化、协议解析四大过载点 | ✅ |
| 3    | 严禁处理硬件消息（如 WM_DEVICECHANGE） | 识别出 `nativeEvent` 违规 | ✅ |
| 4    | 严禁处理物理磁盘 IO（如 mkpath） | 识别出 `onDriveButtonContextMenu` 违规 | ✅ |

## 4. 详细解决方案 (解耦规划)

### 4.1 硬件职责剥离
- **规划**：在 `CoreController` 或新设 `DeviceWatcher` 类中继承 `QAbstractNativeEventFilter`。
- **目标**：将 `WM_DEVICECHANGE` 的处理从 `MainWindow` 物理移除。

### 4.2 业务逻辑下沉
- **规划**：建立 `ManagedFolderService`。
- **目标**：托管库的创建、探测、FRN 获取及 `CategoryRepo` 的原子化写入全部封装进 Service，`MainWindow` 仅负责弹出菜单并调用 Service 接口。

### 4.3 导航逻辑路由化
- **规划**：引入 `NavigationManager`。
- **目标**：`unifiedNavigateTo` 的逻辑迁移至 Manager，Manager 处理协议解析并反向驱动各 Panel 的数据加载。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.cpp`：职责审计与解耦规划。

**明确禁止越界修改的范围：**
- [ ] 本阶段仅产出分析文档，禁止修改任何代码文件。

## 6. 实现准则与预警【核心】
- **预警**：解耦过程需谨慎处理面板间的信号耦合（Data Linkage）。在剥离导航中枢时，需确保各 Panel 的独立性不被破坏。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| MainWindow 职责红线 | 严禁处理硬件消息、物理磁盘 IO 及业务数据持久化逻辑 | ✅ 符合 |
| 信号/事件问题追踪 | 必须完整追踪链路 | ✅ 符合 |

## 8. 待确认事项（可选）
- 建议确认是否需要立即启动解耦工作，还是仅作为技术债务记录。
