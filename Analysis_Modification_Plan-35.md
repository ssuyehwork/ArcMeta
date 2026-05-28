## 分析计划 #35 ｜ [2026-05-28]

### § 0 需求背景
排查全应用中的“傻逼逻辑架构”与“重复定义”，提升项目的工业级鲁棒性与可维护性。

### § 1 全局架构缺陷诊断（Root Cause Analysis）

经过对 `src/ui/` 目录下全量源码的深度审计，发现本项目在快速迭代过程中留下了多处违反工业级软件设计的“架构恶臭”：

**1.1 持久化层面的身份分裂（Configuration Fragmentation）**
- **现象**：`QSettings` 的实例化极度碎片化，且组织/应用名称不统一。
- **证据**：
  - `MainWindow.cpp`: 使用 `QSettings("ArcMeta团队", "ArcMeta")`
  - `ContentPanel.cpp`: 使用 `QSettings("ArcMeta", "ArcMeta")`
  - 甚至在某些 Slot 中临时创建局部 `QSettings` 对象。
- **后果**：配置项散落在注册表/配置文件的不同路径下，导致“设置了这里，那里不生效”的离奇 Bug。

**1.2 物理 UI 常量的黑盒硬编码（Visual Hardcoding）**
- **现象**：色值（Hex）、圆角半径（Radius）、控件尺寸（FixedSize）散落在各 Delegate 的 `paint` 函数中。
- **证据**：
  - `#3498db` (标准蓝) 在 10+ 个文件中重复出现。
  - `setFixedSize(24, 24)` 出现了 20 多次。
  - 列表项高度 `28px` 在多个 Delegate 中独立定义。
- **后果**：若 UI 设计师要求将“标准蓝”改为“品牌紫”，程序员需要物理修改 10 多个文件，极易漏改。

**1.3 核心业务逻辑的“复制粘贴”式实现（Logic Duplication）**
- **现象**：高频使用的 Shell 操作、颜色计算、数据格式化逻辑在不同类中私造。
- **证据**：
  - `SHFileOperationW` (删除) 与 `ShellExecuteExW` (属性) 在 `ContentPanel` 和 `ScanDialog` 中分别实现，甚至参数标志位都不一致。
  - 字节大小转 KB/MB 的逻辑至少存在 3 个版本。
  - 星级绘制逻辑（已在方案 34 排查）是典型的重灾区。
- **后果**：底层 API 调用缺乏统一的错误处理与日志记录。

**1.4 臃肿的“上帝类”中转站（Monolithic Coupling）**
- **现象**：`MainWindow.cpp` 沦为巨型信号中转机。
- **证据**：其 `initUi` 阶段包含了 50+ 个 `connect` 语句，强行将分类、导航、内容、元数据面板耦合在一起。
- **后果**：修改任一面板的信号签名，都会引发 `MainWindow` 的连锁崩溃，维护难度呈指数级上升。

### § 2 “工业级架构”重构方案（The Great Refactoring）

为了彻底根除上述傻逼逻辑，建议按以下模块进行架构物理升级：

#### 2.1 建立 AppConfig 单例（解决持久化混乱）
**方案**：新建 `AppConfig.h/cpp`，物理隔离 `QSettings`。
- **契约**：所有组件禁止直接 new QSettings，必须调用 `AppConfig::instance().getValue()`。
- **收益**：统一存储路径，支持配置项的类型安全（Type Safety）。

#### 2.2 建立物理常量引擎 StyleLibrary（解决硬编码）
**方案**：建立 `StyleLibrary` 全局命名空间。
- **定义**：
  ```cpp
  namespace Style {
      const QColor PrimaryBlue = QColor("#3498db");
      const int StandardIconSize = 18;
      const int RowHeight = 28;
  }
  ```
- **收益**：实现全软件一键换肤能力，消除 Magic Numbers。

#### 2.3 物理封装系统服务层 ShellHelper（解决操作冗余）
**方案**：将所有 Windows 原生调用收归 `ShellHelper` 静态类。
- **接口**：`moveToTrash(path)`, `showProperties(path)`, `openInExplorer(path)`。
- **收益**：统一错误处理机制，例如删除失败时自动弹出标准错误对话框。

#### 2.4 引入 EventBus 机制（解耦 MainWindow）
**方案**：引入中介者模式或全局信号中心。
- **逻辑**：面板 A 发出“路径变更”事件，面板 B/C/D 订阅该事件，不再通过 `MainWindow` 手动拉线。
- **收益**：`MainWindow` 瘦身 70%，组件实现即插即用。

#### 2.5 角色语义契约化（Unified Roles）
**方案**：在 `CommonDefs.h` 中统一定义 `Qt::UserRole`。
- **规范**：`PathRole = Qt::UserRole + 100`, `FidRole = Qt::UserRole + 101`。
- **收益**：确保同一套 Delegate 能在不同 Model 间平滑复用。

### § 3 总结
目前的架构属于典型的“作坊式代码”，通过上述**物理隔离、中心化、契约化**的手段，可将项目质量提升至工业级标准。

---
**⏳ 该文档为全应用架构审计报告，涉及底层重构，需由架构师委员会（用户）审批。**
