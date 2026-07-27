# MainWindow 其余清理 & FilterPanel 解耦 —— Modification_Plan-112.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在对整个应用的总体逻辑架构和核心类进行职责单一原则（SRP）的深度整改和扩大排查中，主窗体 `MainWindow` 及高级筛选面板 `FilterPanel` 被识别出存在明显的职责过载和深度穿透。具体表现为：
1. `MainWindow` 内嵌了大量的无边框拖拽、拉伸热区数值计算（`getResizeDirection`、`updateCursorShape` 等）及鼠标按压/移动内部状态，这些手势已通过 `FramelessWindowResizer` 封装，但在主窗口中依然未作彻底清理（对应用户原话：“完全删除主窗体中冗余的无边框拖拽/拉伸逻辑及事件，切换为纯事件过滤器”）；
2. 主窗口内的 `unifiedNavigateTo` 仍保留了局部的 `m_history` 状态压栈与出栈逻辑，未完全移交和对齐到 `NavigationHistoryService` 单例服务（对应用户原话：“在 MainWindow.cpp 中将旧的 m_history 替换为新解耦的 NavigationHistoryService::instance()”）；
3. 高级过滤面板 `FilterPanel` 将 Adobe Bridge 风格的分组 UI 渲染、色块矩阵事件，同复杂的全局 `FilterState` 业务状态、文件物理特征检索过滤算法（如 Trigram 模糊、时间、大小、备注、链接、多维组合判定、以及最近检索历史）强耦合在一起，违背了表示层与逻辑层彻底解耦的原则（对应用户原话：“将数据检索层（Adobe Bridge 风格的分组 Trigram 模糊、时间、大小、备注、链接、多维组合判定）与 UI 层完全分离，引入解耦的过滤器引擎 FilterEngine”）。

为了根治这些深层架构债，需要针对上述问题执行彻底的物理整改。

## 2. 问题定位
通过代码审计，精确定位如下代码过载位置：
1. **`MainWindow` 的冗余拖动与拉伸事件**：`src/ui/MainWindow.cpp` 中自 946 行至 1047 行的 `mousePressEvent`、`mouseMoveEvent`、`mouseReleaseEvent` 仍包含了拖拽/边缘拉伸物理计算，且保留了 `getResizeDirection`、`updateCursorShape` 成员函数，存在严重的冗余。
2. **`MainWindow` 的历史记录栈**：`src/ui/MainWindow.cpp` 第 1596-1601 行、1680-1689 行等仍旧在通过 `m_history` 处理历史栈。
3. **`FilterPanel` 的过滤判定逻辑过载**：`src/ui/FilterPanel.h` / `src/ui/FilterPanel.cpp` 以及 `src/ui/ContentPanel.cpp`（`FilterProxyModel` 的过滤行筛选，即第 419-530 行及 960-1011 行）直接暴露了对各项多维数据过滤算法的实现。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在 MainWindow.cpp 中将旧的 m_history 替换为新解耦的 NavigationHistoryService::instance()（对应用户原话） | 方案第 4.1 节，删除 MainWindow 内部的历史状态和 `unifiedNavigateTo` 局部的压栈逻辑，完全代理到 `NavigationHistoryService`。 | ✅ |
| 2    | 完全删除主窗体中冗余的无边框拖拽/拉伸逻辑及事件，切换为纯事件过滤器（对应用户原话） | 方案第 4.2 节，彻底物理删除主窗口中 946-1047 行冗余手势，通过已安装的 `FramelessWindowResizer` 统一代理，保留无干涉快捷键及 WA_Hover 属性。 | ✅ |
| 3    | 将数据检索层（Adobe Bridge 风格的分组 Trigram 模糊、时间、大小、备注、链接、多维组合判定）与 UI 层完全分离，引入解耦的过滤器引擎 FilterEngine（对应用户原话） | 方案第 4.3 节，新建 `FilterEngine` 单例/核心引擎，承接 `FilterState` 所有的多维高级过滤计算与检索判定；`FilterPanel` 仅作 UI 数据双向绑定及通知。 | ✅ |

## 4. 详细解决方案

### 4.1 MainWindow 历史栈清理
1. 彻底清除 `MainWindow.h` 中的局部的历史导航记录，确保主窗体内部完全无状态化。
2. 修改 `MainWindow::unifiedNavigateTo(const QString& url, bool record)`：
   - 彻底删除局部 `m_history` mid 截断和 append 过程；
   - 调用 `NavigationHistoryService::instance().recordNavigation(url, record)` 将其移交单例。
3. 修改 `MainWindow::onBackClicked` 和 `onForwardClicked`：
   - 使用 `NavigationHistoryService::instance().canGoBack()` / `canGoForward()` 进行状态判定；
   - 使用 `NavigationHistoryService::instance().goBack()` / `goForward()` 获取目标 URL，然后调用 `unifiedNavigateTo(target, false)` 进行界面刷新。
4. 修改 `MainWindow::updateNavButtons`：
   - 按钮禁用状态更新对齐为：
     ```cpp
     m_btnBack->setEnabled(NavigationHistoryService::instance().canGoBack());
     m_btnForward->setEnabled(NavigationHistoryService::instance().canGoForward());
     ```

### 4.2 完全清除 MainWindow 中冗余的无边框拖拽/拉伸计算
1. 彻底删除 `MainWindow.h` 中无边框相关的鼠标/方向检测函数及相关声明，包括：
   - `enum ResizeDirection`
   - `getResizeDirection`
   - `updateCursorShape`
2. 彻底清空 `MainWindow::mousePressEvent`、`mouseMoveEvent`、`mouseReleaseEvent` 中所有的拖拽/拉伸、边缘热区 DPI 缩放感应逻辑：
   - 在主窗口中仅保留最简易的事件拦截/忽略（即若不属编辑或常规控件交互，直接忽略由已挂载的 `FramelessWindowResizer` 事件过滤器捕获并全盘接管），确保标题栏拖动及八向边缘拉伸完全通过事件过滤器运行；
   - 保留 `keyPressEvent` 等不受拉伸影响的快捷键动作及 `WA_Hover` 状态。

### 4.3 重构 FilterPanel：引入独立过滤器引擎 FilterEngine
1. **新建 `FilterEngine` 类（`src/ui/FilterEngine.h` / `.cpp`）**：
   - 将原有在 `FilterPanel` / `ContentPanel` 内部重复硬编码的过滤算法抽取到 `FilterEngine` 单例中：
     ```cpp
     namespace ArcMeta {
     class FilterEngine {
     public:
         static FilterEngine& instance();
         
         // 执行核心判定：单行是否匹配 FilterState
         bool acceptsRow(const FilterState& filter, const class IngestedRecord& record, const QString& normalizedPath) const;
     };
     }
     ```
   - 判定中无损迁移：评级过滤（`ratings`）、色标过滤（`colors`）、时间、文件大小、链接与备注存在性判定（`linkPresence`、`notePresence`）、宽高比（`ratio`）、多维快速过滤字段以及 Trigram 模糊关键字匹配，确保逻辑物理无损。
2. **重整 `FilterPanel` 为纯 UI 展现层**：
   - 彻底剥离 `saveFilterHistory` 和 `getFilterHistory` 逻辑到 `SearchHistoryService`；
   - 移除 `FilterPanel` 对具体多维过滤判定的强感知，仅在用户发生 checkbox / 色标 / 输入框变化时更新并向 `MainWindow` 发射 `filterChanged(m_filter)` 信号；
3. **改造内容展示代理过滤层**：
   - 修改 `src/ui/ContentPanel.cpp` 中的 `FilterProxyModel::filterAcceptsRow` 和相关的过滤规则，将其直接委托给 `FilterEngine::instance().acceptsRow(...)`，彻底断开展示面板与底层过滤计算的深度交叉耦合。

## 5. 修改边界声明【范围】

**本次整改方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.h`、`src/ui/MainWindow.cpp`（完全清理无边框物理计算和 `m_history`，统一移交单例）
- [ ] 模块/文件：`src/ui/FilterPanel.h`、`src/ui/FilterPanel.cpp`（重塑 FilterPanel 为纯粹数据展示双向绑定层，不再维护具体历史运算）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（将 `FilterProxyModel` 原本杂糅的行筛选过滤逻辑彻底重构，代理到统一计算引擎中）
- [ ] 模块/文件：`src/ui/FilterEngine.h`（新增，过滤判定核心单例接口）
- [ ] 模块/文件：`src/ui/FilterEngine.cpp`（新增，核心过滤与多维 Trigram 判定计算实现）

**明确禁止越界修改的范围：**
- [ ] `DatabaseManager.cpp` 底层 SQLite 连接初始化与备份——不修改
- [ ] `sqlite3.c` 源码——不修改

## 6. 实现准则与预警【核心】
1. **防范编译错误与未定义标识符**：由于引入了全新的 `FilterEngine`，必须在 `CMakeLists.txt` 中正确登记 `FilterEngine.h` 及 `FilterEngine.cpp` 的编译源文件。
2. **保持原有 QSS 样式不受破坏**：`FilterPanel` 剥离过程中，必须绝对保留其已有的 Adobe Bridge 风格 UI 颜色、滑条背景渐变及勾选框视觉效果，不做任何视觉层面的擅自变动。
3. **无损双向状态流**：在主窗口 `unifiedNavigateTo` 接入 `NavigationHistoryService` 时，要确保后退、前进按钮在切换目录时的 `setEnabled` 响应机制完全正常。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 高内聚单一职责 | 业务逻辑层与底层监控/底层服务类完全解耦，不允许底层单例直接调用业务类。 | ✅ 本方案新建的 `FilterEngine` 纯粹为无状态的过滤计算引擎，只向外层提供过滤接受规则，不反向依赖任何 UI 或数据库模块。 |
| 输入框清除功能 | 一律使用 Qt 原生 `setClearButtonEnabled(true)`。 | ✅ 本方案在 `FilterPanel` 新增的各类过滤属性快速输入框中，完全保留并使用该标准。 |

## 8. 待确认事项（可选）
- 暂无。
