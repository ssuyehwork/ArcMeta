# Modification Record

[2026-07-17 15:00:00]
- **任务描述**: 标题栏新增品牌 Logo 功能实现 (Plan-79)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h` (新增 `m_logoLabel` 成员变量)
    - **修改**: `src/ui/MainWindow.cpp` (在 `setupSplitters` 中初始化 Logo 并将其插入标题栏最左侧)
- **修改原因**: 强化品牌识别度，在标题栏 "FERREX" 文本前增加品牌矢量 Logo。
- **优化点**:
    - **视觉平衡**: 采用 18x18px 尺寸，配合 16x16 的 Logo 渲染尺寸，确保在 34px 高度的标题栏中与品牌文字实现视觉对齐。
    - **品牌一致性**: 统一使用 `BrandOrange` (#FF551C) 作为 Logo 与 "FERREX" 文本的颜色，增强品牌感。
    - **布局细节**: 将标题栏前段间距上调至 8px，使 Logo 与文字的呼吸感更自然。
    - **性能友好**: 使用 `UiHelper` 直接从 `SvgIcons` 内联资源加载，无额外文件 I/O。

[2026-07-17 14:30:00]
- **任务描述**: 标题栏布局管理按钮及分栏重置功能实现 (Plan-78)。
- **修改文件**:
    - **修改**: `src/ui/MainWindow.h` (新增 `m_btnLayout` 成员及 `resetSplitterLayout` 声明)
    - **修改**: `src/ui/MainWindow.cpp` (实现布局按钮点击菜单、重置逻辑，并移除旧有面板右键菜单逻辑)
    - **修改**: `src/core/AppConfig.h` (补全 `remove` 成员函数，修复编译错误)
- **修改原因**: 提升交互确定性，废弃隐蔽且冲突的右键触发模式，改为显式的标题栏按钮控制，并提供一键恢复默认布局的功能。
- **优化点**:
    - **交互收敛**: 彻底取消所有面板容器及标题栏对右键菜单的冒泡触发，避免与子控件右键冲突。
    - **原子重置**: `resetSplitterLayout` 确保先 `show()` 后 `setSizes()`，并同步清除 `AppConfig` 缓存，防止重启回滚。
    - **视觉一致性**: 采用 `layout.svg` 图标，保持 24x24px 标题栏按钮规范。

[2026-07-16 10:45:00]
- **任务描述**: 升级“批量重命名”界面的下拉框（QComboBox）UI。实现圆角设计，并将线条箭头替换为实心向下三角形。
- **修改文件**:
    - `src/ui/RuleRow.cpp`
    - `src/ui/BatchRenameDialog.cpp`
    - `src/ui/SvgIcons.h` (新增紧凑型三角形图标)
- **未修改文件**:
    - `src/ui/BatchRenamePreviewDialog.cpp` (无下拉框组件)
- **优化点**:
    - **UI 参数二次重算**：为追求极致的大气感，将实心三角形图标进一步优化为 12x12 规格，并将 QSS 参数上调（Arrow 12px, DropDown 24px），确保视觉饱满、清晰可见。
    - **布局细节优化**：统一了预设区组件间距为 5px，并将删除按钮调整为持续灰色高亮显示。
    - **性能优化**：引入了 `static const QString` 缓存图标物理路径，解决了 `RuleRow` 在大批量行渲染时的 IO 性能瓶颈。
- **潜在风险**:
    - QSS 中的图标路径在不同操作系统下的兼容性需保持关注（已通过 `UiHelper::getSvgTempFilePath` 处理）。
    - 25px 的锁定高度可能导致三角形图标在某些缩放比例下显示偏小。
[2026-06-17 05:10:51]
- **任务描述**: 确立并记录“范围感知 (Scope-Aware)”核心开发准则，分析双轨制逻辑架构与搜索框重构方案。
- **修改文件**:
    - `AGENTS.md` (记录红线规则)
- **新增文件**:
    - `Analysis_Modification_Plan/Analysis_Modification_Plan-54.md` (架构分析报告)
- **未修改文件**:
    - `src/ui/MainWindow.cpp` (仅分析逻辑)
    - `src/ui/ContentPanel.cpp` (仅分析逻辑)
- **风险提示**:
    - 确立此规则后，后续所有功能开发（当局者身份）必须强制进行“焦点状态检查”，增加了逻辑判断的复杂度，但确保了用户体验的一致性。

[2026-07-xx]
- **任务描述**: 实现 MetadataManager 多维隔离索引架构，并修复搜索结果 UI 渲染断层问题。
- **修改文件**:
    - `src/meta/MetadataManager.h/.cpp`
    - `src/core/CoreController.cpp`
    - `src/ui/MainWindow.cpp`
    - `src/ui/ContentPanel.h/.cpp`
- **核心优化**:
    - **隔离索引架构**：物理分离文件、文件夹及后缀名索引，彻底规避同名冲突。
    - **Loading Token 机制**：为异步加载流程引入令牌校验，解决了“过期扫描任务覆盖最新搜索结果”的严重竞态 Bug。
    - **搜索链路加固**：物理清除搜索时的筛选器干扰，支持流式结果追加。
- **风险提示**:
    - 令牌机制依赖 `m_currentLoadToken` 的单调递增，需确保所有异步加载入口均已接入该机制以防止数据丢失。

[2026-06-19 11:45:10]
任务描述：更新 AGENTS.md 规范（新增实现准则与预警章节），并产出符合新规的话题 5 审计报告。
修改文件：`AGENTS.md` (更新), `Analysis_Modification_Plan/Analysis_Modification_Plan-66.md` (新增)
未修改文件：全部 C++ 源码文件
潜在风险：无。
