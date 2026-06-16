# 架构分析与修改方案 - 窗口置顶逻辑原生化重构（彻底解决窗口消失 Bug）

## 1. 现状剖析与缺陷诊断

### 1.1 问题描述
当前系统中，除 `MainWindow` 外，所有基于 `FramelessDialog` 的对话框（如“批量重命名”窗口）在执行置顶操作时，会出现窗口瞬间消失或掉到主窗口后方的严重 Bug。

### 1.2 逻辑根因
审计 `src/ui/FramelessDialog.cpp` 发现，置顶逻辑使用了 Qt 标志位：
```cpp
setWindowFlag(Qt::WindowStaysOnTopHint, checked);
show();
```
- **核心缺陷**：在 Windows 上修改 `WindowFlag` 会强制触发窗口的销毁与重新创建（Destroy & Recreate）。
- **连锁反应**：新创建的窗口会丢失原有层级关系，由于主窗口通常持有焦点，子窗口在重建后可能被压在主窗口下方，造成“消失”的视觉 Bug。

---

## 2. 解决方案建议（已达成共识）

### 2.1 强制原生化原则
废除所有非主窗口界面的 `Qt::WindowStaysOnTopHint` 逻辑，全量迁移至 **Win32 原生 `SetWindowPos`** 方案。

### 2.3 技术重构方案（以 FramelessDialog 为例）
修改 `FramelessDialog.cpp` 中的置顶按钮回调逻辑：

**建议代码：**
```cpp
#ifdef Q_OS_WIN
    #include <windows.h>
#endif

// ... 在按钮触发槽函数中
#ifdef Q_OS_WIN
    HWND hwnd = (HWND)winId();
    SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    show();
#endif
```
**优点**：直接在 OS 层面修改窗口层级，不涉及 Qt 窗口对象的销毁与重建，坐标与焦点保持绝对稳定。

---

## 3. AGENTS.md 规则制定

为了防止后续开发中再次引入此类缺陷，必须在 `AGENTS.md` 中增设红线规则。

**新增规则内容：**
> ### 5.2 窗口置顶实现规范
> - **唯一标准**：所有窗口（包括主窗口、对话框、悬浮窗）的置顶/取消置顶功能，必须且只能使用 **Win32 原生 `SetWindowPos`** API（配合 `HWND_TOPMOST` / `HWND_NOTOPMOST` 标志）。
> - **严禁使用**：禁止使用 `setWindowFlag(Qt::WindowStaysOnTopHint)` 或任何会导致窗口重建的标志位操作。
> - **性能优化**：调用时必须配合 `SWP_NOSENDCHANGING` 标志，以拦截冗余消息风暴，确保置顶切换无卡顿、无闪烁。

---

## 4. 结论
通过将置顶逻辑从“Qt 层”下降到“系统原生层”，可彻底根除因窗口重建导致的层级丢失问题，确保全软件所有弹出窗口的交互稳定性达到工业级水准。
