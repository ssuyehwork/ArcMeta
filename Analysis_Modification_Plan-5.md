# Analysis_Modification_Plan-5: 彻底废除 ScanDialog 模块及全量相关逻辑

## 1. 目标与动机
根据用户最新指令，系统将彻底“核平” `ScanDialog.cpp` 及其关联的所有逻辑。该模块原用于全盘 MFT 极速扫描与搜索，由于架构调整（转向一盘一库 SQLite 内存模式），该独立的扫描窗口及其附属的 `ScanController` 已不再符合当前的极简设计理念，需彻底从物理与逻辑层面抹除。

## 2. 逻辑清理与移除清单

### 2.1 待物理删除的文件 (Physical Removal)
- `src/ui/ScanDialog.h` & `src/ui/ScanDialog.cpp`：扫描窗口 UI 与模型实现。
- `src/ui/ScanController.h` & `src/ui/ScanController.cpp`：扫描核心调度器。
- `src/ui/ScanDialog_Audit_Report.md`：相关的审计文档。

### 2.2 待清理的逻辑分支与耦合 (Code Cleanup)

#### MainWindow 相关清理
- **UI 按钮移除**：
  - 在 `MainWindow::setupCustomTitleBarButtons` 中，删除 `m_btnScan` 的创建、样式设置及其信号连接逻辑。
  - 在 `MainWindow.h` 中删除 `m_btnScan` 成员变量声明。
- **引用清理**：
  - 删除 `MainWindow.cpp` 中对 `#include "ScanDialog.h"` 的引用。

#### 核心与配置清理
- **缓存管理**：
  - 在 `src/core/CacheManager.h` 和 `src/core/CoreController.cpp` 中，删除所有提及 `ScanDialog` 的注释、标志位或相关缓存路径常量（如针对全盘 `.scch` 的特殊说明）。

## 3. 架构影响评估
- **扫描功能去向**：全盘 MFT 扫描与索引重建逻辑将不再通过独立的对话框呈现。
- **UI 纯净化**：主窗口标题栏将减少一个按钮（`scan` 按钮），使得界面更加紧凑，符合“一盘一库”背景下的自动化静默工作流。

## 4. 清理执行步骤
1. **解除耦合**：首先修改 `MainWindow.cpp`，注释并删除所有与 `ScanDialog` 及 `m_btnScan` 相关的代码行。
2. **物理删除**：执行 `rm` 命令删除上述列出的头文件、实现文件及 Markdown 审计报告。
3. **全局搜索**：再次全局搜索关键字 "ScanDialog" 和 "ScanController"，确保没有任何残留的注释或无效引用。

---
**旁观者意见**：这是对 ArcMeta UI 架构的一次重大简化。通过移除独立的扫描对话框，我们实际上是将“全盘索引”从一个用户显式操作的功能，转化为了一种后台静默的基础服务。这不仅减少了代码量，也让用户的操作路径更加聚焦。
