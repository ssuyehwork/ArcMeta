全应用逻辑架构审计与模块化重构方案 —— Analysis_Modification_Plan-119.md

## 1. 任务背景
针对当前应用 `src/` 目录下的逻辑架构进行深度审计，识别其中不合理（“傻逼”）的设计、僵尸代码以及需要合并的模块。旨在通过纯分析师视角，为后续的架构优化提供清晰的蓝图。

## 2. 问题定位

### 2.1 逻辑架构不合理项（“傻逼”逻辑）
1.  **上帝类 (God Class) 风险 —— `MainWindow` 职责过载**：
    *   **现象**：`MainWindow` 类不仅管理 UI 组件的布局与显隐，还深度参与了**导航调度**（`unifiedNavigateTo` 协议解析）、**历史记录维护**（`m_history`）、**搜索防抖**（`m_searchTimer`）以及**原生 Windows 消息拦截**。
    *   **影响**：UI 层与业务逻辑层高度耦合，导致单元测试困难，且任何非 UI 逻辑的修改都可能意外触发布局抖动。
2.  **导航逻辑硬编码且侵入 UI**：
    *   **现象**：`unifiedNavigateTo` 直接在 `MainWindow.cpp` 中通过 `if-else` 判断 `file://`, `category://`, `system://` 协议。
    *   **影响**：当需要新增协议（如 `tags://`）时，必须修改核心 UI 类，违反了开闭原则（OCP）。
3.  **向上寻道 (Upward Searching) 依赖**：
    *   **现象**：`ContentPanel.cpp` 等子组件通过 `qobject_cast<MainWindow*>(parentWin)` 的方式向上寻找 `MainWindow` 以复用菜单逻辑或调用导航。
    *   **影响**：子组件无法脱离 `MainWindow` 独立存在，破坏了组件的封装性。

### 2.2 僵尸代码 (Zombie Code) 定位
1.  **空占位与无效 TODO**：
    *   `src/ui/MainWindow.cpp:1569`: `// TODO: 盘符点击逻辑待后期安排`。此处的盘符点击仅打印 `qDebug`，无实际业务产出。
    *   `src/ui/TagManagerView.cpp:326`: `// TODO: 常用标签逻辑（目前暂无权重统计，显示为空）`。
2.  **无谓的调试残留**：
    *   散落在 `MainWindow.cpp` 中的大量 `qDebug()`（如 `[Main] Request rescan for...`），属于开发期的脚手架代码，在稳定版中已成为干扰日志。

### 2.3 模块合并与重构契机
1.  **导入逻辑碎片化**：
    *   `src/util/ImportHelper`（执行物理迁移）与 `src/core/AutoImportManager`（监听 MFT 自动入库）职能重合（对应用户原话：“哪些逻辑架构需要合并成模块？”）。
2.  **导航组件散乱**：
    *   `AddressBar`, `BreadcrumbBar`, `AddressHistoryPanel` 均为导航服务，但缺乏统一的控制器管理（对应用户原话：“哪些逻辑架构需要合并成模块？”）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 审核整个应用的逻辑架构存在多少傻逼逻辑架构？ | 2.1 节详细列举了上帝类、硬编码协议及高度耦合问题 | ✅ |
| 2    | 存在多少僵尸代码？ | 2.2 节定位了 TODO 占位、空实现及调试残留 | ✅ |
| 3    | 哪些逻辑架构需要合并成模块？ | 2.3 节提出了 Navigation、Import 等模块化建议 | ✅ |

## 4. 详细解决方案

### 4.1 架构解耦方案：引入控制器层
1.  **抽象 `NavigationController`**：
    *   将 `unifiedNavigateTo` 逻辑迁移至此处。
    *   维护全局导航状态（`currentUrl`）和历史堆栈。
    *   UI 组件（`AddressBar`, `NavPanel`）通过信号槽或单例订阅状态变化，不再直接操作 `MainWindow`。
2.  **抽象 `SearchService`**：
    *   封装搜索防抖逻辑。
    *   作为 `CoreController` 与 UI 搜索框之间的缓冲层，处理 `FilterState` 的原子修改（对应 AGENTS.md 规范：“搜索框输入内容 = 修改 FilterState.keyword”）。

### 4.2 模块合并建议
1.  **合并为 `DataIngestionModule` (数据摄取模块)**：
    *   整合 `ImportHelper` 与 `AutoImportManager`（对应用户原话：“哪些逻辑架构需要合并成模块？”）。
    *   统一“物理移动”与“元数据注册”的事务性流程，确保物理移动成功后元数据同步更新。
2.  **合并为 `NavigationModule` (导航模块)**：
    *   将 `AddressBar`, `BreadcrumbBar` 封装在统一的 `NavigationWrapper` 内，由 `NavigationController` 驱动（对应用户原话：“哪些逻辑架构需要合并成模块？”）。

### 4.3 僵尸代码清理计划
1.  **物理删除标记为 TODO 且长期未变动的空函数体**（对应用户原话：“存在多少僵尸代码？”）。
2.  **引入全局日志宏替代原生调试输出**。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 逻辑审计：`src/core`, `src/ui`, `src/util`
- [ ] 架构建议：导航逻辑、搜索行为、数据导入流程

**明确禁止越界修改的范围：**
- [ ] 禁止修改任何 `.cpp` 或 `.h` 文件。
- [ ] 禁止对 `旧版本-N` 文件夹进行任何形式的处理。

## 6. 实现准则与预警【核心】
1.  **依赖预警**：若后续执行重构，相关控制器必须先行实现，因为它将成为连接各个面板的新纽带。
2.  **编译风险**：解耦 `MainWindow` 时，需注意原生事件拦截逻辑的完整性。
3.  **一致性检查**：所有搜索相关的重构必须通过 `FilterState` 字段，严禁直接调用 `CoreController::performSearch` 绕过过滤机制。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 范围感知搜索 | 必须实时对标 UI 顶部的蓝色提示线 | ✅ |
| 搜索框行为 | 修改 FilterState.keyword + 触发本地过滤 | ✅ |
| 架构红线 6.2 | 严禁走独立于 FilterState 的数据加载流程 | ✅ |

## 8. 待确认事项
1.  **关于 `AutoImportManager` 的同步策略**：在合并模块时，是否需要保留当前的 MFT 强依赖？
2.  **UI 向上寻道重构**：是否允许引入 `ServiceLocator` 模式来替代当前的 `parentWin` 查找逻辑？
3.  **重构顺序**：方案提及的控制器层实现是否存在优先级顺序？
