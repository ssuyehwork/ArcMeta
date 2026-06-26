# MainWindow 过重与架构耦合分析 —— Analysis_Modification_Plan-106.md

## 1. 任务背景
当前任务是对 `ArcMeta/src` 的现有版本进行逻辑架构分析，判断当前代码是否存在复制粘贴式实现、职责混合、适应性差和运行体验不佳的架构问题。

## 2. 问题定位
### 主要问题
- `MainWindow` 类承担了过多职责，已从窗口视图扩展为业务调度器、状态机、导航中心、搜索控制器、元数据同步器和 UI 事件聚合点。
- `MainWindow` 与数据层的耦合过高，直接调用 `MetadataManager`、`CategoryRepo`、`AppConfig`、`ToolTipOverlay` 等多个底层模块。
- `CoreController` 目前仅负责启动初始化和搜索，无法承担真正的应用中枢职责，导致控制层不完整。

### 代码位点
- `src/main.cpp`：启动入口、日志、单实例、主窗体构造、后台初始化触发。
- `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`：大量 UI 组件与业务逻辑混合，且包含统一导航、搜索、防抖、历史、模式切换、元数据更新、预览控制等多种职责。
- `src/core/CoreController.cpp`：只负责 `startSystem()` 和 `performSearch()`，未形成完整协作层。

## 3. 强制对照表
| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1 | 你要分析当前版本，不看旧版本 | 仅分析 `ArcMeta/src` 当前源码，不依赖 `旧版本-*` 目录 | ✅ |
| 2 | 判断代码逻辑架构是否不理想 | 分析 `MainWindow` 过重、UI/业务混合、层次不清 | ✅ |
| 3 | 说明是否存在复制粘贴式问题 | 指出重复逻辑片段、状态散布、功能未抽象 | ✅ |

## 4. 详细解决方案
### 4.1 拆分 `MainWindow` 职责
- 保留 `MainWindow` 作为纯视图层，负责窗口布局、控件创建和 UI 事件传递。
- 将导航历史、路径协议解析、内容跳转抽离为独立 `NavigationController` 或 `NavigationState`。
- 将搜索、过滤状态、搜索防抖、异步结果处理抽离为独立 `SearchController` 或 `FilterCoordinator`。
- 将元数据变更、标签同步、颜色/星级持久化抽离为 `MetadataCoordinator` 或 `MetadataService`。

### 4.2 降低 UI 与数据层耦合
- `MainWindow` 不直接调用 `MetadataManager::instance().setRating(...)` / `setColor(...)` / `setTags(...)`。
- 让 `MainWindow` 通过信号/槽或协作层发布“元数据修改请求”，由单独模块执行持久化。
- 让 `MainWindow` 只与 `ContentPanel` / `FilterPanel` / `CategoryPanel` 等 UI 面板交换状态，而非直接读取 `PathRole`、`RatingRole` 等底层字段。

### 4.3 统一搜索与筛选流
- 将 `m_searchEdit` 的输入和 `m_filterPanel` 的筛选条件合并到统一 `FilterState`。
- 让 `ContentPanel` 只消费当前状态，而不是由 `MainWindow` 在多个回调中分别调用 `applyFilters()`、`search()`、`loadDirectory()`。

### 4.4 控制层完整性
- 让 `CoreController` 承担“系统服务协调”职责，包含初始化、搜索、状态广播、错误通知。
- 让 `MainWindow` 作为 `CoreController` 的一个消费者，而不是“所有逻辑的最终归属者”。

## 5. 修改边界声明【红线】
**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.h`
- [ ] `src/ui/MainWindow.cpp`
- [ ] `src/core/CoreController.h`
- [ ] `src/core/CoreController.cpp`
- [ ] `src/meta/MetadataManager.h` / `cpp`
- [ ] `src/meta/CategoryRepo.h` / `cpp`

**明确禁止越界修改的范围：**
- [ ] `旧版本-*` 目录下的内容
- [ ] 资源文件、第三方库、外部工具配置
- [ ] 与当前版本无关的历史实现或分支代码

## 6. 实现准则与预警【核心】
- `MainWindow` 只保留 UI 布局与事件监听，不应承担跨模块业务逻辑。
- 所有跨模块调用应通过信号/槽或协作层，不要再直接调用 `MetadataManager`、`CategoryRepo`、`AppConfig`。
- 任何涉及 `QDir`、`QFileInfo`、路径规范的逻辑应封装到独立服务，避免视图层直接处理这些底层数据。
- `ToolTipOverlay` 的多处调用应统一封装，避免同一类提示逻辑多点复制。
- `CoreController::performSearch()` 目前没有与 `MainWindow` 的本地过滤机制完全对齐，需要确认“搜索框是否应该直接触发本地代理模型过滤而不是后端搜索”。

## 7. Memories.md 合规检查
| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI/业务分离 | 视图层不承担业务调度 | ✅ |
| 事件驱动 | 采用信号/槽替代直接调用 | ✅ |
| 单一职责 | `MainWindow` 不应过度膨胀 | ✅ |

## 8. 待确认事项
- 是否希望在后续阶段将 `NavigationController`、`SearchController`、`MetadataCoordinator` 设计为单例，还是作为 `MainWindow` 的独立成员？
- 当前搜索框是否应保留本地过滤优先级，还是统一交给 `CoreController` 后端搜索？
- `ToolTipOverlay` 是否允许保留为全局单例，仅封装提示文本逻辑？
