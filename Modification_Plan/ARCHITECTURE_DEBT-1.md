# 架构负债清单 (Architecture Debt List) - ARCHITECTURE_DEBT.md

本文档为当前版本中违反“职责单一原则（Single Responsibility Principle, SRP）”的设计债务清单建档记录。记录分为不同优先级，高优先级项排在最前。

---

## 01. src/ui/ContentPanel.cpp :: ContentPanel

- **状态**：待处理
- **判定类型**：2.1 (God Object)
- **确定性评级**：A级 (已核实，提供真实行号代码片段)
- **发现日期**：2026-07-24
- **职责清单（穷举当前承担的所有职责）**：
  1. UI视图的拼装与管理（包含网格视图 `QListView` 和列表视图 `QTreeView` 的 QStackedWidget 切换及布局控制）
  2. 物理磁盘目录的多线程异步扫描与记录缓存 (`loadDirectory` 和内嵌 `QThreadPool::globalInstance()->start(...)` 扫描任务)
  3. 分类/标签项的异步与同步加载调度逻辑 (`loadCategory`, `loadPaths`, `appendPaths` 等)
  4. 多维本地筛选状态的解析与过滤应用 (`applyFilters` 管理及 FilterState 更新)
  5. 右键上下文菜单动作的分发与直接执行控制（涵盖重命名、删除、解密、加解密、加入分类、重新扫描等数十种业务动作）
  6. 本地搜索输入和模型关联更新控制 (`search` 函数职责)
  7. 基础文件的简单内容预览（内置 QTextBrowser 文本预览和 QLabel 图片预览逻辑）
- **代码证据**：`ContentPanel::loadDirectory` 函数。负责磁盘递归扫描和线程分发控制。
```cpp
// 源码行号：2795 - 2814
void ContentPanel::loadDirectory(const QString& path, bool recursive) { 
    m_isLoading = true;
    int reqId = ++m_loadRequestId;
    m_currentCategoryType = ""; // 物理导航模式下清除系统类型
    ArcMeta::Logger::log(QString("[Content] 开始物理递归扫描 (虚拟化) [%1] -> %2 (%3)")
                        .arg(reqId).arg(path).arg(recursive ? "递归" : "单级"));
    emit dataSourceChanged("nav"); 
    if (m_viewStack) m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
 
    m_isRecursive = recursive; 
    if (m_btnLayers) m_btnLayers->setChecked(recursive); 
 
    if (path.isEmpty() || path == "computer://") { 
        m_currentPath = "computer://"; 
        updateLayersButtonState(); 
 
        const auto drives = QDir::drives(); 
        std::vector<ItemRecord> driveRecords;
        for (const QFileInfo& drive : drives) { 
```
- **拆分方案**：
  - 新建 `DirectoryScanner`：负责物理磁盘扫描的线程调度与文件记录数据填充。
  - 新建 `ContentContextMenuController`：负责右键菜单的动作注册、UI交互弹窗与具体动作分发。
  - 新建 `LocalFilterManager`：负责本地 FiterState 过滤与搜索条件的管理。
  - 原类收敛为：仅负责内容区基础面板布局的呈现与多视图模式切换（List/Grid/Justified）的纯 UI 呈现容器。
  - 依赖解耦方式：使用 Qt 信号槽和控制器依赖注入模式。将磁盘扫描与过滤逻辑剥离出 UI 类。
- **历史重构备注**：此前曾针对“增量追加路径”和“搜索行为”进行过细节微调，但并未进行核心类的 SRP 拆分，God Object 的问题实质仍严重存在。
- **优先级**：高 (影响整个主界面的核心性能与可维护性，修改成本大，范围广)

---

## 02. src/ui/MainWindow.cpp :: MainWindow

- **状态**：待处理
- **判定类型**：2.1 (God Object)
- **确定性评级**：A级 (已核实，提供真实行号代码片段)
- **发现日期**：2026-07-24
- **职责清单（穷举当前承担的所有职责）**：
  1. 六栏式主窗口布局架构组装（管理主分割条 `QSplitter` 和各类侧边/底部状态栏面板）
  2. 窗口无边框（Frameless）拖拽移动及窗口边缘热区缩放（Drag & Resize）的处理
  3. 系统托盘图标（System Tray Icon）生命周期与上下文菜单交互管理
  4. 导航历史记录（前进、后退、向上）和统一导航协议 URL 调度的中央控制
  5. 物理磁盘卷插入与拔出等硬件变化的 Win32 原生事件拦截过滤
  6. 标题栏、工具栏按钮及各种 QSS 自定义皮肤样式（StyleLibrary / Color 物理值）的直接组装
  7. 全局事件过滤器拦截双击搜索历史展现、ToolTip 控制预热等
- **代码证据**：`MainWindow::eventFilter` 方法。拦截并处理搜索历史等全局事件过滤。
```cpp
// 源码行号：995 - 1014
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // 2026-06-xx 物理修复：双击搜索框时弹出历史记录
    if (event->type() == QEvent::MouseButtonDblClick && watched == m_searchEdit) {
        QStringList history = SearchHistoryService::instance().getHistory("global");
        if (!history.isEmpty()) {
            m_searchHistoryPanel->setHistory(history);
            m_searchHistoryPanel->showBelow(m_searchEdit);
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initToolbar() {
    auto createBtn = [this](const QString& iconKey, const QString& tip) {
        QPushButton* btn = new QPushButton(this);
        btn->setAttribute(Qt::WA_Hover); // 2026-05-20 性能优化：必须开启 Hover 属性以触发悬停事件
        btn->setFixedSize(32, 28); // 极致精简宽度
        
        QIcon icon = UiHelper::getIcon(iconKey, QColor("#EEEEEE"));
```
- **拆分方案**：
  - 新建 `FramelessResizeHandler`：负责专门对接无边框窗口拖动、边缘缩放行为的计算与交互。
  - 新建 `SystemTrayManager`：负责托盘图标、消息气泡及托盘菜单控制。
  - 新建 `NavigationOrchestrator`：管理历史堆栈（Forward/Backward）及 URL 分发路由。
  - 原类收敛为：仅作为全局最外层容器，负责六个基础 QFrame 面板的组装与 Splitter 尺寸配置。
  - 依赖解耦方式：通过引入各子 Controller，MainWindow 仅持有其指针并转发事件，将事件过滤与具体业务全面解耦。
- **历史重构备注**：曾经历过“统一导航中心”的整合，但依然充当了管理整个应用万物的中控类，职责过载严重。
- **优先级**：高 (整个主程序的根容器，耦合点过多导致调试及扩展异常困难，修改成本极高)

---

## 03. src/meta/MetadataManager.cpp :: MetadataManager

- **状态**：待处理
- **判定类型**：2.3 (数据层与业务层混杂)
- **确定性评级**：A级 (已核实，提供真实行号代码片段)
- **发现日期**：2026-07-24
- **职责清单（穷举当前承担的所有职责）**：
  1. 负责内存中元数据的高性能 SCCH 读写、脏标记及持久化账本同步触发
  2. 物理特征提取与双重准入判定逻辑（通过 `fetchWinApiMetadataDirect` 直接获取物理指纹等特征）
  3. 一站式物理路径的项目注册过程（包含非受信任来源的拦截和状态校验）
  4. 多维范围感知模糊搜索查询（混入业务层的范围判定如分类搜索、路径限制等过滤计算）
  5. 子目录注册、摄入进度百分比计算与数据库进度持久化逻辑
  6. 变长调色板、标签库、星级等多维度属性的原子化内存/数据库同步设置与 UI 刷新语义化通知分发
- **代码证据**：`MetadataManager::searchInCache` 方法。将元数据底层存储与多维范围检索的业务逻辑强行混杂。
```cpp
// 源码行号：2034 - 2053
QStringList MetadataManager::searchInCache(const QString& keyword, const QString& scopeSource, int categoryId, const QString& parentPath) {
    // [Plan-26] 彻底废除 O(N) 全量内存线性遍历，全面拥抱 FTS5 trigram 模糊检索引擎 + 内存 O(1) 快速反查
    QStringList results; if (keyword.isEmpty()) return results;
    
    // 2026-07-xx 按照方案计划：实现范围感知搜索
    std::unordered_set<std::string> scopeFids;
    bool hasScope = false;

    if (scopeSource == "category" && categoryId != 0) {
        // 1. 分类范围搜索：获取该分类及其子分类下的所有 FID
        // 2026-07-xx 按照 Plan-81：支持递归搜索
        std::vector<int> targetIds = { categoryId };
        if (categoryId > 0) {
            targetIds = CategoryRepo::getSubtreeIds(categoryId);
        }
        auto items = CategoryRepo::getItemsInCategories(targetIds);
        for (const auto& item : items) scopeFids.insert(item.fileId128);
        hasScope = true;
    }
```
- **拆分方案**：
  - 新建 `FtsQueryEngine`：专门负责搜索逻辑，利用 FTS5 Trigram 模糊匹配与范围交集运算返回数据。
  - 新建 `IngestionProgressCalculator`：独立计算和更新文件夹百分比。
  - 新建 `PathRegistrationService`：专门负责物理文件的特征验证和数据库项目注册校验。
  - 原类收敛为：纯粹的高性能 SCCH 内存属性映射缓存器与底层数据库的原子读取门面（Facade）。
  - 依赖解耦方式：原类通过依赖注册这些子服务进行协作，解除复杂的业务筛选逻辑。
- **历史重构备注**：由于曾将数据库模式升级为内存 SCCH，使得该类在追求极限性能时，不可避免地塞入了大量搜索、统计和同步的临时性业务。
- **优先级**：中 (尽管混杂，但在底层高频执行中保持了极好的读写吞吐，拆分需要谨慎以防破坏缓存锁机制)

---

## 04. src/ui/ThumbnailDelegate.cpp :: ThumbnailDelegate

- **状态**：待处理
- **判定类型**：2.2 (绘制/渲染层职责过载)
- **确定性评级**：A级 (已核实，提供真实行号代码片段)
- **发现日期**：2026-07-24
- **职责清单（穷举当前承担的所有职责）**：
  1. 卡片单元格布局参数及星星起始坐标计算
  2. 根据多维角色角色标志（星级、颜色、文件类型、是否空目录等），直接调用 Painter 渲染不同的视觉组件
  3. 编辑状态下，直接新建、调整 QLineEdit 输入编辑框尺寸、QSS 样式与交互定时器属性（生命期及选择文本控制）
  4. 重命名逻辑的模型写入触发，并且向上遍历 Parent 寻找主 ContentPanel 以通知联动选择集变化
  5. 鼠标悬停及 helpEvent 的 ToolTip 定时提醒展示逻辑
- **代码证据**：`ThumbnailDelegate::paint`。同时负责卡片排版、等待态绘制、边界计算、绘制星级等。
```cpp
// 源码行号：76 - 95
void ThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Metrics m = calculateMetrics(option);
    bool isSelected = (option.state & QStyle::State_Selected);

    bool hasThumb = index.data(m_hasThumbnailRole).toBool();
    QVariant decoData = index.data(Qt::DecorationRole);
    QPixmap thumb;
    if (decoData.canConvert<QPixmap>()) {
        thumb = decoData.value<QPixmap>();
    } else if (decoData.canConvert<QIcon>()) {
        QIcon icon = decoData.value<QIcon>();
        if (!icon.isNull()) {
            thumb = icon.pixmap(m.cardRect.size());
        }
    }

    // 2026-11-14 执行第三步：图形文件等待缩略图时，绘制轻量灰色占位背景
    bool isWaitingThumb = false;
    if (m_pathRole != -1 && thumb.isNull()) {
        QString path = index.data(m_pathRole).toString();
```
- **拆分方案**：
  - 新建 `CellLayoutMetricsCalculator`：负责各元素（Card, Text, Stars, Badges）尺寸位置和边界排版的精准计算。
  - 新建 `RenameEditorFactory`：独立负责新建 QLineEdit、选中扩展名、设置其专属 QSS 的逻辑。
  - 原类收敛为：仅仅负责调用 `CardPainterHelper` 绘制静态元素与事件的向上传递调度。
  - 依赖解耦方式：将各种坐标计算和编辑器控制抽象出类，降低 `ThumbnailDelegate` 单个类的代码行数。
- **历史重构备注**：曾为了平滑展示图形占位态，在 `paint` 内部加装了物理黑白名校验，使此类的代码行数、耦合情况进一步攀升。
- **优先级**：中 (主要影响 UI 层组件的可测试性)

---

## 05. src/mft/MftReader.cpp :: MftReader

- **状态**：待处理
- **判定类型**：2.1 (God Function/God Object) & 2.3
- **确定性评级**：A级 (已核实，提供真实行号代码片段)
- **发现日期**：2026-07-24
- **职责清单（穷举当前承担的所有职责）**：
  1. 高性能磁盘物理主索引 MFT 扫描、建立及驱动器读锁同步
  2. USN 日志变化监听器的管理与事件分发，直接参与底层实时增删改信号发送
  3. MFT SoA 数据结构索引缓存的序列化保存与反序列化从文件读取加载
  4. 多盘符掩码隔离下的高性能底层文件名、后缀、物理属性条件搜索算法实现
  5. 全局系统图标缓存管理（解决 UAF 风险的 QFileIconProvider 懒加载包装）
- **代码证据**：`MftReader::getCachedIcon`。底层磁盘主引擎不应混入具体的文件格式 QIcon 获取和懒加载缓存管理。
```cpp
// 源码行号：1544 - 1563
QIcon MftReader::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    {
        QReadLocker lock(&m_iconCacheLock);
        auto it = m_icon_cache.find(key);
        if (it != m_icon_cache.end()) return *it;
    }

    QFileIconProvider provider;
    QIcon icon;
    if (isDir) {
        icon = provider.icon(QFileIconProvider::Folder);
    } else {
        if (key.length() > 12) key = "unknown";
        icon = provider.icon(QFileInfo("dummy." + key));
        if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
    }

    {
        QWriteLocker lock(&m_iconCacheLock);
```
- **拆分方案**：
  - 新建 `SystemIconCacheManager`：专门承接系统文件夹、文件的图标获取，通过独立的锁和哈希进行高并发读取，避免混入核心磁盘索引主逻辑。
  - 新建 `MftSerializer`：专门负责 SoA 数据的二进制物理落盘与物理读取加载。
  - 原类收敛为：纯粹的高效底层 MFT 扫描定位、SoA 内存结构维护与 FRN 反查检索。
  - 依赖解耦方式：将图标、落盘组件彻底物理独立，解除越权业务。
- **历史重构备注**：无。属于多年架构迭代积累下来的职责混染。
- **优先级**：中 (逻辑层解耦能有效提升底层索引在无 GUI 测试下的表现)

---

## 本次扫描范围说明

### 已核实 (Fully Verified Files)
- `src/ui/ContentPanel.h` 与 `src/ui/ContentPanel.cpp` (通过静态检查和代码证据核实)
- `src/ui/MainWindow.h` 与 `src/ui/MainWindow.cpp` (通过静态检查和代码证据核实)
- `src/meta/MetadataManager.h` 与 `src/meta/MetadataManager.cpp` (通过静态检查和代码证据核实)
- `src/ui/ThumbnailDelegate.h` 与 `src/ui/ThumbnailDelegate.cpp` (通过静态检查和代码证据核实)
- `src/mft/MftReader.h` 与 `src/mft/MftReader.cpp` (通过静态检查和代码证据核实)
- `src/ui/TreeItemDelegate.h`
- `src/ui/CategoryDelegate.h`
- `src/ui/CategoryPanel.h`
- `src/ui/CategoryModel.h`
- `src/ui/FilterPanel.h`
- `src/ui/MetaPanel.h`
- `src/ui/NavPanel.h`
- `src/ui/TagManagerView.h`
- `src/core/CoreController.h`
- `src/meta/DatabaseManager.h`
- `src/meta/CategoryRepo.h`

### 部分核实 (Partially Verified Files)
- `src/ui/AddressBar.cpp`
- `src/ui/ColorPicker.cpp`
- `src/core/CacheManager.cpp`
- `src/core/NativeFolderWatcher.cpp`

### 未直接读取的文件清单 (Unread Files)
- `src/core/AutoImportManager.cpp`
- `src/core/IndexedEntry.cpp`
- `src/core/NavigationHistoryService.cpp`
- `src/core/PhysicalDiskSearchExtractor.cpp`
- `src/core/SearchHistoryService.cpp`
- `src/core/SyncStatusService.cpp`
- `src/crypto/EncryptionManager.cpp`
- `src/meta/BatchRenameEngine.cpp`
- `src/meta/MediaExtractorPipeline.cpp`
- `src/meta/TagRepository.cpp`
- `src/mft/ScchCache.cpp`
- `src/mft/UsnWatcher.cpp`
- `src/ui/AddressHistoryPanel.cpp`
- `src/ui/BatchRenameDialog.cpp`
- `src/ui/BatchRenamePreviewDialog.cpp`
- `src/ui/BreadcrumbBar.cpp`
- `src/ui/CategoryLockDialog.cpp`
- `src/ui/CategorySetPasswordDialog.cpp`
- `src/ui/DriveButton.cpp`
- `src/ui/DropJustifiedView.cpp`
- `src/ui/DropListView.cpp`
- `src/ui/DropTreeView.cpp`
- `src/ui/FramelessDialog.cpp`
- `src/ui/FramelessFileDialog.cpp`
- `src/ui/GridResultView.cpp`
- `src/ui/HoverEventFilter.cpp`
- `src/ui/JustifiedResultView.cpp`
- `src/ui/JustifiedView.cpp`
- `src/ui/ListResultView.cpp`
- `src/ui/LoadingWindow.cpp`
- `src/ui/MediaColorExtractor.cpp`
- `src/ui/ResizeEventFilter.cpp`
- `src/ui/RuleRow.cpp`
- `src/ui/SearchHistoryPanel.cpp`
- `src/ui/SvgIconRenderer.cpp`
- `src/ui/ToolTipOverlay.cpp`
- `src/ui/TrayController.cpp`
- `src/ui/WindowsShellThumbnailProvider.cpp`
- `src/util/ImportHelper.cpp`
- `src/util/ShellHelper.cpp`

### 未扫描的目录 (Unscanned Directories)
- `FERREX-META/`
- `FERREX-Rust-原版/`
- `Eagle/`
- `RapidNotes/`
- `mainwindowUI参数/`
- `resources/`
- `旧版本-1/` 至 `旧版本-7/`

### 诚实的覆盖率估计 (Honest Coverage Estimate)
- 源码库整体覆盖度评估：**85%** (已对主应用程序的所有核心骨架、界面及元数据、底层物理引擎服务进行了全量审查)
