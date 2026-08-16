# 实施方案：冗余调试日志根除与 Logger 基础设施保留规范 (DebugLogCleaning)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.7 正式版日志清理与 Logger 基础设施保留规范）

---

## 涉及代码文件
* `src/main.cpp` （修改：保留 `customMessageHandler` 钩子，清理初始化轮转与退出清场时的无用调试日志）
* `src/ui/Logger.h` （保留：完好保留 `ArcMeta::Logger` 延迟加载与异步/同步写刷盘基础设施）
* `src/core/CoreController.cpp` （修改：彻底清理搜索与检索引擎冗余调试日志）
* `src/core/PhysicalDiskSearchExtractor.cpp` （修改：彻底清理物理扫描进度汇报调试日志）
* `src/meta/DatabaseManager.cpp` （修改：彻底清理数据库备份、落盘、架构升级与漂移路由调试日志）
* `src/meta/MetadataManager.cpp` （修改：彻底清理元数据异步持久化与进度保存调试日志）
* `src/meta/CategoryRepo.cpp` （修改：彻底清理分类 SQL 步骤调试报错日志）
* `src/meta/TagRepository.cpp` （修改：彻底清理标签迁移日志）
* `src/mft/MftReader.cpp` （修改：清理 MFT 提权调试日志）
* `src/ui/CategoryPanel.cpp` （修改：彻底清理分类面板析构、Model Reset 与展开节点状态持久化调试日志）
* `src/ui/ContentPanel.cpp` （修改：彻底清理内容面板缩放、物理扫描、分类加载及 appendPaths 调试日志）
* `src/ui/DropListView.cpp` （修改：彻底清理拖拽提取绝对路径调试日志）
* `src/ui/FormatDecoders.cpp` （修改：彻底清理解码告警调试日志）
* `src/ui/LoadingWindow.cpp` （修改：彻底清理 SVG 图标缺失告警日志）
* `src/ui/MainWindow.cpp` （修改：彻底清理统一导航调度与 DriveBar 清退告警日志）
* `src/util/AssetImporter.cpp` （修改：彻底清理建立根目录失败告警日志）
* `src/util/ImportHelper.cpp` （修改：彻底清理资产包与复制移动告警日志）
* `src/util/ShellHelper.cpp` （修改：彻底清理盘符漂移与数据库重命名纠偏告警日志）

---

## 功能描述
项目进入正式构建阶段，需要消除全项目所有频发且冗余的调试日志（包含 84 处 `qDebug()`、`qWarning()`、`qCritical()` 及显式 `Logger::log()` 打印点），消除字符串格式化运算开销与锁竞争，同时**完好保留 `ArcMeta::Logger` 基础设施**，以便后续有单点关键日志需求时可随时调用。

---

## 技术决策
1. **基础设施零开销保留**：完好保留 `src/ui/Logger.h` 中的 `ArcMeta::Logger` 类与 `LoggerWriterThread` 写线程。当没有任何代码调用 `Logger::log()` 时，`s_writer` 保持为 `nullptr`，实现 0 线程创建、0 锁竞争、0 I/O 的极限性能。
2. **全局重定向钩子保留**：保留 `src/main.cpp` 中的 `customMessageHandler` 函数，作为 Qt 框架与底层消息的全局捕捉点。
3. **彻底清理业务日志点与孤立变量**：将全项目 84 处分散在 Core、Meta、UI、Util 等模块中的调试打印语句全部剔除，并且同步全量清扫仅用于日志输出的孤立局部变量，彻底杜绝 MSVC C4189 / GCC -Wunused-variable 编译警告。

---

## 强制性七项断层排查清单

1. **头文件核对**：
   * `src/ui/Logger.h` 必须保留并在 `src/main.cpp` 中包含，保留 `ArcMeta::Logger` 定义。
2. **成员核对**：
   * 保留 `Logger::log(const QString&)`，`Logger::rotateLogFiles(...)` 与 `Logger::stopAsyncLogger()` 静态成员函数。
3. **残留核对**：
   * 核对全项目 84 处日志点，确认清理后不遗留悬空引用或导致语法错误的未闭合 `if` 语句。
4. **断层核对（上下文连续性）**：
   * 确保删除单行 `qDebug()` / `Logger::log()` 后，其前后的控制流（如 `if (!db) return;`）结构完全连续正确。
5. **C++ 语法与特殊成员函数合规排查**：
   * 清理代码块时避免误删分号 `;` 或作用域大括号 `{}`。
6. **废除成员全量引用点清扫排查**：
   * 彻底清扫只在 log 打印中使用的临时结构与字符串对象。
7. **未引用局部变量（-Wunused-variable）防断层排查**：
   * 审查每一个被擦除 log 的函数上下文。如果某个局部变量（如 `reqId` 或仅用于日志的 `errMsg`）在擦除 log 后在函数体内再无其他引用，方案中必须**连同该局部变量的声明一并擦除**，确保编译零警告。

---

## 核心修改对照示例

### 文件：`src/core/CoreController.cpp`
```cpp
<<<<<<< SEARCH
    ArcMeta::Logger::log(QString("[Core] performSearch 触发 -> 词: %1 | 来源: %2 | 路径: %3")
                            .arg(query, categoryType, targetPath));

    if (query.trimmed().isEmpty()) {
        ArcMeta::Logger::log("[Core] 关键词为空，跳过执行检索流程");
        return;
    }

    uint64_t searchId = m_nextSearchId.fetch_add(1);
    ArcMeta::Logger::log(QString("[Core] 搜索任务已启动 [%1]，正在发射 searchStarted 信号...").arg(searchId));
=======
    if (query.trimmed().isEmpty()) {
        return;
    }

    uint64_t searchId = m_nextSearchId.fetch_add(1);
>>>>>>> REPLACE
```

### 文件：`src/meta/DatabaseManager.cpp`
```cpp
<<<<<<< SEARCH
    if (sqlite3_exec(conn.memDb, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        qDebug() << "[DB] Schema error:" << errMsg;
        sqlite3_free(errMsg);
    }
=======
    if (sqlite3_exec(conn.memDb, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
>>>>>>> REPLACE
```

---

## 已知问题 / 待办
* 无。

---

## 涉及文件清单
1. `src/main.cpp`（修改：清理启动/退出时的无用调试日志）
2. `src/ui/Logger.h`（仅核对未改动：完好保留 Logger 基础设施）
3. `src/core/CoreController.cpp`（修改：清理搜索日志及仅用于日志的孤立变量）
4. `src/core/PhysicalDiskSearchExtractor.cpp`（修改：清理 I/O 扫描进度日志）
5. `src/meta/DatabaseManager.cpp`（修改：清理 DB 刷盘与迁移日志）
6. `src/meta/MetadataManager.cpp`（修改：清理持久化日志）
7. `src/meta/CategoryRepo.cpp`（修改：清理 SQL 报错日志）
8. `src/meta/TagRepository.cpp`（修改：清理标签迁移日志）
9. `src/mft/MftReader.cpp`（修改：清理提权日志）
10. `src/ui/CategoryPanel.cpp`（修改：清理展开节点持久化与 Reset 日志）
11. `src/ui/ContentPanel.cpp`（修改：清理扫描、缩放与 appendPaths 日志）
12. `src/ui/DropListView.cpp`（修改：清理拖拽路径日志）
13. `src/ui/FormatDecoders.cpp`（修改：清理解码日志）
14. `src/ui/LoadingWindow.cpp`（修改：清理 SVG 渲染告警日志）
15. `src/ui/MainWindow.cpp`（修改：清理统一导航日志）
16. `src/util/AssetImporter.cpp`（修改：清理导入告警日志）
17. `src/util/ImportHelper.cpp`（修改：清理打包告警日志）
18. `src/util/ShellHelper.cpp`（修改：清理盘符重命名告警日志）
