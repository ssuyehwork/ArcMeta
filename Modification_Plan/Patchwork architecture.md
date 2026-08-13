# 打补丁 —— Patchwork architecture

本文档系统性排查并记载了当前 ArcMeta 客户端中存在的采用“临时打补丁（Workaround）”、“临时保护校验（Zero Guarding）”或“绕开机制（Bypass Hacking）”的缝缝补补型代码设计。所有记录均通过实际源码走查核实，未进行任何代码文件的物理修改。

---

## 01. `src/ui/CategoryPanel.cpp` :: 计数空值更新“僵尸”拦截

- **状态**：待处理
- **判定类型**：临时保护与无效拦截补丁
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **代码证据**：`CategoryPanel` 的 UI 局部更新槽函数。
```cpp
// 源码行号：110 - 117
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, sysCounts, catCounts]() {
                if (weakThis && weakThis->m_categoryModel) {
                    // 物理修复：若统计数据全为0，且系统元数据尚未加载完成，则拒绝执行 UI 更新以防止计数清零
                    if (sysCounts.isEmpty() && catCounts.isEmpty()) {
                        return;
                    }
                    weakThis->m_categoryModel->updateStatistics(sysCounts, catCounts);
```
- **打补丁根因与危害分析**：
  - **根因**：因为系统在异步初始化时存在时序竞争，如果在元数据未完全载入时开始对账，`fullRecount()` 算出的 counts 将全部为 `0` 并持久化刷回 `system_stats` 数据库（造成 Dirty Write）。为了在不治理该异步时序的前提下防止界面显示 `0`，开发在此处加装了 `isEmpty()` 拦截。
  - **补丁危害**：该补丁不仅治标不治本，而且是一个**物理失效的僵尸补丁**。因为 `CategoryRepo::getSystemCounts()` 永远会返回一个包含 6 个系统默认键（如 `"all"`, `"trash"` 等）的 Map，即使值全为 `0`，`sysCounts.isEmpty()` 也恒为 `false`。该防御机制实际上在运行时无法拦截任何零值更新，归零 Bug 依然会穿透到 UI 上。

---

## 02. `src/ui/FramelessDialog.cpp` / `ToolTipOverlay.cpp` / `LoadingWindow.cpp` :: Windows 平台 Win32 置顶物理补丁

- **状态**：待处理
- **判定类型**：平台特定 API 侵入式绕过补丁
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **代码证据**：`FramelessDialog::showEvent` 及其构造函数。
```cpp
// 源码行号：93 - 99 (FramelessDialog.cpp)
        // 2026-06-xx 物理修复：废弃 Qt 标志位操作，改用 Win32 原生 API 以防止窗口重建消失 Bug
        ::SetWindowPos((HWND)winId(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
```
```cpp
// 源码行号：18 - 22 (ToolTipOverlay.cpp)
    // 2026-06-xx 物理修复：通过原生 API 实现置顶，避免标志位导致的重建问题
    ::SetWindowPos((HWND)winId(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
```
- **打补丁根因与危害分析**：
  - **根因**：标准的 Qt 标志置顶操作（`setWindowFlag(Qt::WindowStaysOnTopHint)`）在底层会强制销毁底层的 OS 窗口句柄并进行重建，这在 Windows 平台下会导致窗口剧烈闪烁并丢失已有的渲染或输入焦点。
  - **补丁危害**：各子窗口通过直接包含 Windows 系统头文件、现场编写 C 类型的硬编码强转 `(HWND)winId()` 来调用 Windows 原生 API。这种侵入式的补丁缺乏统一的窗口管理抽象类（如 WindowStateManager），极大地破坏了代码的跨平台可移植性（Mac/Linux 编译链将直接报未定义符号错误），属于脏逻辑套用。

---

## 03. `src/ui/TreeItemDelegate.h` :: 行内编辑器 `singleShot` 延迟时序补丁

- **状态**：待处理
- **判定类型**：基于 Qt 事件循环的异步时序 hack
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **代码证据**：`TreeItemDelegate::updateEditorGeometry` 或重写 `createEditor`。
```cpp
// 源码行号：348 - 355
        // 物理修复：使用 QTimer 确保在 Qt 默认 selectAll 之后执行，防止逻辑被覆盖
        QTimer::singleShot(0, editor, [editor]() {
            if (editor) {
                editor->selectAll();
            }
        });
```
- **打补丁根因与危害分析**：
  - **根因**：在 QTreeView 触发双击行内编辑拉起 `QLineEdit` 时，Qt 内部框架默认会在事件队列稍后执行 `selectAll()` 或移动光标动作。如果开发想在创建编辑器时自定义光标位置或全选行为，常规的同步代码会被 Qt 框架后续的默认行为覆盖。
  - **补丁危害**：利用 `QTimer::singleShot(0, ...)` 借用事件循环的异步排队特性（即“下一次事件循环时执行”）来抢在 Qt 渲染逻辑之后、并在 QLineEdit 展现前强行执行全选。这是一种典型的微秒级时序补丁，一旦操作系统面临高 CPU 占用或渲染排队延迟，该异步回调的时序将变得不稳定，容易导致编辑时全选失效或光标闪烁等疑难杂症。

---

## 04. `src/ui/FilterPanel.cpp` :: 异步加载“猜谜型”重绘拦截补丁

- **状态**：待处理
- **判定类型**：基于猜谜（Heuristics）的 UI 保护阻断补丁
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **代码证据**：`FilterPanel::updateFilterState` 或数据过滤改变响应函数。
```cpp
// 源码行号：536 - 542
    // 2026-06-xx 物理修复：若所有输入均为空，且当前没有活动的文本过滤，则判定为异步加载中间态，拒绝执行重绘以防止 UI 抖动
    if (allInputsEmpty && !hasActiveTextFilter) {
        return;
    }
```
- **打补丁根因与危害分析**：
  - **根因**：在多选分类或者切换受控库分类的瞬间，UI 会临时重置所有的筛选卡片与文本框，导致输入条件瞬间变为空集并触发一次重绘（Full Repaint），这会产生极难看的一瞬间频闪（抖动）。
  - **补丁危害**：在 UI 响应层硬编码进行“猜谜”。UI 根本不知道当前是否真的处于“异步加载中间态”，仅仅是通过“所有输入框是否都为空”来主观猜测系统的加载意图。如果用户真的只是单纯地清空了所有过滤条件、想要重新展示全部数据，这一拦截也可能会被误触发，从而导致重置过滤器时界面不重绘的静默失效 Bug。应该采用严密的加载状态机（Loading States）统一管控，而非在 View 内部自写脏过滤。

---

## 05. `src/ui/ContentPanel.cpp` :: 禁用向筛选面板发送“全空”统计信号补丁

- **状态**：待处理
- **判定类型**：临时保护遮罩
- **确定性评级**：A级 (已通过阅读实际源码确认的事实)
- **发现日期**：2026-08-13
- **代码证据**：`ContentPanel::recalculateAndEmitStats`。
```cpp
// 源码行号：3255 - 3259
        // 2026-06-xx 物理修复：严禁向筛选面板发送“全空”统计信号，防止因时序重叠导致筛选条件被强制清零或被覆盖
        if (statsData.isEmpty()) {
            return;
        }
```
- **打补丁根因与危害分析**：
  - **根因**：当分类切换、数据重新加载时，由于异步查询尚未返回，统计引擎计算出了空的数据统计。如果不加控制地将空统计向外发送给 `FilterPanel`，会导致筛选面板上本已配置好的过滤项（比如某个颜色分类）因为可用统计归零而被强制隐藏、置空。
  - **补丁危害**：这种“直接截断空数据”的补丁虽然临时绕开了筛选条件被误清空的 BUG，但当整个分类在物理盘上确实没有文件（真的是空文件夹）时，由于统计被强行截断，筛选面板上仍会显示着上一个文件夹遗留下来的旧统计选项和高亮状态（数据未被重置），给用户造成“这里面有文件可筛”的严重视觉误导。

---

## 本次排查与分析范围说明

- **已核实并直接读取源码（A级）的文件清单**：
  - `src/ui/CategoryPanel.cpp`
  - `src/ui/FramelessDialog.cpp`
  - `src/ui/ToolTipOverlay.cpp`
  - `src/ui/TreeItemDelegate.h`
  - `src/ui/FilterPanel.cpp`
  - `src/ui/ContentPanel.cpp`
- **尚未验证的假设**：
  - 假定在后续重构中，通过引入全局的 `LoadingState` 状态管理以及强耦合的时序同步通知，这些补丁可以被一并整体物理移除且不引发任何界面闪烁。
