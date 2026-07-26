# 角色定义
你是一位精通 C++20、Qt 6 框架以及高并发多线程软件设计的资深 C++ 首席架构师。

# 任务目标
请对当前项目仓库（包含 src/ 目录下的所有 .h / .cpp 文件）进行一次地毯式的【代码质量、架构合理性与性能隐患】深度审计。重点查找项目中是否存在“补丁堆砌”、“职责错位”、“性能死穴”以及“线程/生命周期不安全”等硬伤。

# 排查清单（四大核心维度）

请根据以下具体的技术特征，逐个文件扫描并定位代码问题：

## 维度一：补丁堆砌与规避式编程（Patchwork Anti-Patterns）
1. **滥用 QTimer::singleShot(0, ...)**：查找试图通过“延迟到下一个 Tick”来强行掩盖 UI 竞争、控件未生成或信号同步顺序混乱的代码。
2. **强行锁信号/标志位**：查找大量使用 `blockSignals(true/false)`、`m_isRestoringState`、`m_isFirstLoad` 等标志位来阻断信号死循环的代码（说明模块间没有单向数据流）。
3. **魔数与特殊字符串流转**：查找使用 `"__RELOAD_ALL__"`、负数 ID 或特定魔法字符串来触发全量刷新的临时补丁。
4. **变通式重复写入（Workaround）**：查找在 UI/右键菜单回调里，因调用 `setData` 不生效而手动补充调用数据库/底层 repo 更新的代码。

## 维度二：职责错位与单一职责原则违背（Misplaced Responsibilities）
1. **UI 上帝类（God Class）**：检查 `ContentPanel`、`CategoryPanel`、`MainWindow` 等视图层组件是否直接编写了 SQLite/SQL 操作、文件系统物理删改（如 `QFile::remove`、递归物理抹除）、COM 调用或复杂的算法逻辑。
2. **Model 层混入物理逻辑**：检查 `QAbstractItemModel` 子类（如 `FerrexVirtualDbModel`）是否在 `data()` 函数中同步读取磁盘、随机频繁访问 `QFileInfo`，或者直接做业务落盘。
3. **main.cpp 成为垃圾桶**：检查 `main.cpp` 中是否存在预热单例、业务逻辑处理、非必要全局对象构造等越界行为。

## 维度三：性能毒药与阻塞隐患（Performance Bottlenecks）
1. **主线程/锁下物理 I/O**：查找在主 UI 线程或全局 `QMutex` 保护下同步调用 `QFile::open`、`logFile.flush()`、SQLite 同步 `exec` 的行为。
2. **过度全量重置（beginResetModel / FullRebuild）**：查找在增量数据变更时，错误调用 `beginResetModel()` 或抛出全局重构信号，导致 UI 视图丢失选中态、卡顿、首帧闪烁的代码。
3. **视图 data() 中的磁盘/开销陷阱**：查找在 Delegate 绘制或 Model 的 `data()` 函数中频繁创建 `QFileInfo`、`QSvgRenderer`，或在循环中反复查询数据库的代码。

## 维度四：线程亲和性与生命周期安全隐患（Threading & Lifecycle Risks）
1. **单例跨线程哑死**：查找在构造函数里创建了 `QTimer`，但可能在子线程中首次被调用 `instance()` 导致定时器没有 `QEventLoop` 活度的单例。
2. **缺乏 Clean Shutdown**：检查程序退出（如 `aboutToQuit`）时，是否存在未释放的堆对象、未关闭的 SQLite 连接、未等待退场的线程池（`waitForDone`）或未成对的 `CoUninitialize()`。
3. **线程池裸启动无控**：查找大量使用 `QThreadPool::globalInstance()->start` 启动异步任务但缺乏取消令牌（Cancel Token）或弱指针（`QPointer`）保护的代码（容易引发析构后野指针野回调崩溃）。

---

# 输出报告格式要求

请将排查结果汇总整理为一份结构清晰的 `Architectural_Audit_Report.md` 报告，格式如下：

### 1. 严重隐患汇总表（Summary Table）
| 风险等级 | 文件路径 | 行号范围 | 问题类型 | 核心缺陷概述 |
|---|---|---|---|---|
| 🔴 致命/高危 | `src/ui/ContentPanel.cpp` | L230-L250 | 性能/选中丢失 | 设色后触发 beginResetModel 导致 UI 选中状态丢失 |
| 🟡 中危 | `src/main.cpp` | L86-L95 | 补丁堆砌 | 强制点名预热单例掩盖线程亲和性缺陷 |

### 2. 详细缺陷剖析（Detailed Analysis）
针对表中的每个重点问题，按以下模板给出深入分析：
- **缺陷位置**：`文件路径:行号`
- **代码片段**：展示有问题原代码
- **技术根因**：解释为什么这样写是“补丁/职责错位/性能隐患”
- **潜在危害**：会导致什么 Bug（如卡顿、崩溃、数据损坏、交互失焦）
- **重构建议**：给出专业的 C++ 代码修正方案

请现在开始全库扫描并输出报告！