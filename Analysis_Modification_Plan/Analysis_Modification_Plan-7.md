# 地址栏功能增强架构分析与方案 (Analysis_Modification_Plan-7)

## 1. 需求背景
为了提升路径导航的交互效率与可追溯性，地址栏需从单纯的“路径展示/输入”进化为具备“刷新响应”与“历史记忆”的复合功能组件。

## 2. 核心功能设计

### 2.1 刷新机制 (Refresh Logic)
- **刷新按钮**：
    - **UI 布局**：在 `AddressBar` 布局最右侧插入一个 `QPushButton`。使用 `UiHelper::getIcon("sync", ...)` 加载图标。
    - **样式对齐**：采用与标题栏按钮一致的 `QPushButton { background: transparent; border: none; }` 样式，悬浮显示 `#FFFFFF1A` 背景。
- **快捷键支持 (F5)**：
    - **实现路径**：在 `MainWindow::keyPressEvent` 中捕获 `Qt::Key_F5`，或者在 `AddressBar` 中安装事件过滤器拦截 F5。
    - **动作分发**：触发 `pathChanged(m_currentPath)` 信号，驱动 `ContentPanel` 重新执行 `loadDirectory` 以扫描物理磁盘变更。

### 2.2 路径历史持久化 (Path History Persistence)
- **存储方案**：集成至 `AppConfig`，键名设为 `AddressBar/History` (类型：`QStringList`)。
- **记录时机**：在 `AddressBar::setPath` 或 `onPathEditFinished` 成功跳转后，将新路径去重并置顶存入列表（上限建议 15 条）。
- **物理 vs 虚拟**：仅持久化真实的物理磁盘路径，对于“此电脑”等系统虚拟路径，可根据 UX 需求决定是否记录。

### 2.3 双击展示历史面板 (Floating History Panel)
- **触发逻辑**：重写 `AddressBar` 或其内部 `m_pathStack` 的 `mouseDoubleClickEvent`。
- **复用方案**：参考 `SearchHistoryPanel` 的实现，创建一个 `AddressHistoryPanel`。
- **定位与显示**：利用 `panel->showBelow(m_pathStack)` 实现精准的下拉悬浮效果。用户单击历史项时，通过 `AddressBar::pathChanged` 信号触发页面跳转。

## 3. 架构改动点预测 (Anticipated Changes)

### 3.1 AddressBar 类扩展
- 成员变量：增加 `QPushButton* m_btnRefresh`。
- 方法：增加 `saveToHistory(const QString& path)`。
- 信号：增加 `refreshRequested()`。

### 3.2 MainWindow 协同
- 监听 `AddressBar::refreshRequested()`，调用 `m_contentPanel->refreshAll()`。

## 4. 结论
通过将地址栏与 `AppConfig` 深度绑定并引入成熟的悬浮面板组件，可以极大地增强文件系统的导航韧性。该方案在保持 UI 简洁的同时，赋予了地址栏“时间维度”的导航能力。
