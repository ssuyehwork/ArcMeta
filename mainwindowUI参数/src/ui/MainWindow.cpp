#include "ToolTipOverlay.h"
#include "MainWindow.h"
#include <QDebug>
#include <QListView>
#include <QTreeView>
#include "StringUtils.h"
#include "TitleEditorDialog.h"
#include "../core/DatabaseManager.h"
#include "NoteDelegate.h"
#include "CategoryDelegate.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <utility>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QElapsedTimer>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QShortcut>
#include <QItemSelection>
#include <QActionGroup>
#include <QColorDialog>
#include <QSet>
#include <QSettings>
#include <QRandomGenerator>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QFile>
#include <QBuffer>
#include <QCoreApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
#include <QMimeData>
#include <QPlainTextEdit>
#include "CleanListView.h"
#include "../core/FileStorageHelper.h"
#include "FramelessDialog.h"
#include "CategoryPasswordDialog.h"
#include "PasswordVerifyDialog.h"
#include "../core/ShortcutManager.h"
#include <functional>
#include "../core/ActionRecorder.h"
#include <QVariant>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#define RESIZE_MARGIN 10
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent, Qt::FramelessWindowHint) {
    setWindowTitle("RapidNotes");
    setAcceptDrops(true);
    resize(1200, 800);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    initUI();

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    refreshData();

    // 【关键修改】区分两种信号
    // 1. 增量更新：添加新笔记时不刷新全表
    connect(&DatabaseManager::instance(), &DatabaseManager::noteAdded, this, &MainWindow::onNoteAdded);
    
    // 2. 全量刷新：修改、删除、分类变化（锁定状态）时才刷新全表 (通过 scheduleRefresh 节流)
    connect(&DatabaseManager::instance(), &DatabaseManager::noteUpdated, this, &MainWindow::scheduleRefresh);
    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, &MainWindow::scheduleRefresh, Qt::QueuedConnection);

    connect(&DatabaseManager::instance(), &DatabaseManager::activeCategoryIdChanged, this, [this](int id){
        // [CRITICAL] 核心修复：只有当外部（如快速笔记窗口）强制切换到一个具体的有效分类 (>0) 时，
        // 或者当前确实处于分类模式且需要同步为“取消选中”(-1) 时，才执行状态转换。
        // 这能有效防止点击“今日数据”、“全部数据”等系统项时，被此信号误杀回“未分类”状态。
        if (id > 0) {
            if (m_currentFilterType == "category" && m_currentFilterValue == id) return;
        } else {
            // id == -1 的情况
            if (m_currentFilterType != "category") return; // 当前已是系统模式（如今日、全部），无需处理
            if (m_currentFilterValue == -1) return; // 已经是未分类模式，无需重复刷新
        }

        m_currentFilterType = "category";
        m_currentFilterValue = id;
        m_currentPage = 1;
        scheduleRefresh();
    });

    restoreLayout(); // 恢复布局
    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &MainWindow::updateShortcuts);
    
    // [CRITICAL] 顶级事件监听：确保在任何子控件获焦时，MainWindow 都能第一时间截获 Ctrl+S 等物理按键。
    installEventFilter(this);
}

void MainWindow::initUI() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    centralWidget->setMouseTracking(true);
    centralWidget->setAttribute(Qt::WA_StyledBackground, true);
    centralWidget->setStyleSheet("#CentralWidget { background-color: #1E1E1E; }");
    setCentralWidget(centralWidget);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. HeaderBar
    m_header = new HeaderBar(this);
#ifdef RAPID_MANAGER_TARGET
    // 2026-03-xx 按照用户最高要求：管理终端彻底独立，强制修改标识
    if (auto* title = m_header->findChild<QLabel*>()) {
         if (title->text() == "快速笔记") title->setText("独立管理终端");
    }

    // 物理隐藏所有非本界面的功能入口（工具箱、全局锁定、托盘联动等）
    QList<QPushButton*> buttons = m_header->findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        QString tip = btn->property("tooltipText").toString();
        if (tip.contains("工具箱") || tip.contains("全局锁定") || tip.contains("设置") || tip.contains("帮助")) {
            btn->hide();
            btn->setEnabled(false);
        }
    }
#else
    if (auto* title = m_header->findChild<QLabel*>()) {
         if (title->text() == "快速笔记") title->setText("快速笔记");
    }
#endif

    connect(m_header, &HeaderBar::searchChanged, this, [this](const QString& text){
        m_currentKeyword = text;
        m_currentPage = 1;
        m_searchTimer->start(300);
    });
    connect(m_header, &HeaderBar::pageChanged, this, [this](int page){
        m_currentPage = page;
        refreshData();
    });
    connect(m_header, &HeaderBar::refreshRequested, this, &MainWindow::refreshData);
    connect(m_header, &HeaderBar::stayOnTopRequested, this, [this](bool checked){
        if (auto* win = window()) {
            if (win->isVisible()) {
#ifdef Q_OS_WIN
                HWND hwnd = (HWND)win->winId();
                SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
                Qt::WindowFlags f = win->windowFlags();
                if (checked) f |= Qt::WindowStaysOnTopHint;
                else f &= ~Qt::WindowStaysOnTopHint;
                win->setWindowFlags(f);
                win->show();
#endif
            }
        }
    });
    connect(m_header, &HeaderBar::filterRequested, this, [this](){
        bool visible = !m_filterWrapper->isVisible();
        m_filterWrapper->setVisible(visible);
        m_header->setFilterActive(visible);
        if (visible) {
            m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
        }
    });
    connect(m_header, &HeaderBar::newNoteRequested, this, &MainWindow::doNewIdea);
    connect(m_header, &HeaderBar::toggleSidebar, this, [this](){
        m_sidebarContainer->setVisible(!m_sidebarContainer->isVisible());
        // 2026-03-13 修复逻辑：切换侧边栏可见性后，立即刷新焦点线状态
        updateFocusLines();
    });
    connect(m_header, &HeaderBar::toolboxRequested, this, &MainWindow::toolboxRequested);
    connect(m_header, &HeaderBar::metadataToggled, this, [this](bool checked){
        m_metaPanel->setVisible(checked);
    });
    connect(m_header, &HeaderBar::windowClose, this, &MainWindow::close);
    connect(m_header, &HeaderBar::windowMinimize, this, &MainWindow::showMinimized);
    connect(m_header, &HeaderBar::windowMaximize, this, [this](){
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    mainLayout->addWidget(m_header);

    // 核心内容容器：管理 5px 全局边距
    auto* contentWidget = new QWidget(centralWidget);
    contentWidget->setAttribute(Qt::WA_StyledBackground, true);
    contentWidget->setStyleSheet("background: transparent; border: none;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(5, 5, 5, 5); // 确保顶栏下方及窗口四周均有 5px 留白
    contentLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(5); // 统一横向板块间的物理缝隙为 5px
    splitter->setChildrenCollapsible(false);
    splitter->setAttribute(Qt::WA_StyledBackground, true);
    splitter->setStyleSheet("QSplitter { background: transparent; border: none; } QSplitter::handle { background: transparent; }");

    // 1. 左侧侧边栏包装容器 (固定 230px)
    auto* sidebarWrapper = new QWidget();
    sidebarWrapper->setMinimumWidth(230);
    auto* sidebarWrapperLayout = new QVBoxLayout(sidebarWrapper);
    sidebarWrapperLayout->setContentsMargins(0, 0, 0, 0); // 彻底消除偏移边距，由全局 Layout 和 Splitter 控制

    m_sidebarContainer = new QFrame();
    m_sidebarContainer->setMinimumWidth(230);
    m_sidebarContainer->setObjectName("SidebarContainer");
    m_sidebarContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_sidebarContainer->setStyleSheet(
        "#SidebarContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* sidebarShadow = new QGraphicsDropShadowEffect(m_sidebarContainer);
    sidebarShadow->setBlurRadius(10);
    sidebarShadow->setXOffset(0);
    sidebarShadow->setYOffset(4);
    sidebarShadow->setColor(QColor(0, 0, 0, 150));
    m_sidebarContainer->setGraphicsEffect(sidebarShadow);

    auto* sidebarContainerLayout = new QVBoxLayout(m_sidebarContainer);
    sidebarContainerLayout->setContentsMargins(0, 0, 0, 0); 
    sidebarContainerLayout->setSpacing(0);

    m_sidebarFocusLine = new QWidget();
    m_sidebarFocusLine->setFixedHeight(1);
    m_sidebarFocusLine->setStyleSheet("background-color: #2ecc71;");
    m_sidebarFocusLine->hide();
    sidebarContainerLayout->addWidget(m_sidebarFocusLine);

    // 侧边栏标题栏 (全宽下划线方案)
    auto* sidebarHeader = new QWidget();
    sidebarHeader->setFixedHeight(32);
    sidebarHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* sidebarHeaderLayout = new QHBoxLayout(sidebarHeader);
    sidebarHeaderLayout->setContentsMargins(15, 0, 15, 0);
    auto* sbIcon = new QLabel();
    sbIcon->setPixmap(IconHelper::getIcon("category", "#3498db").pixmap(18, 18));
    sidebarHeaderLayout->addWidget(sbIcon);
    auto* sbTitle = new QLabel("分类");
    sbTitle->setStyleSheet("color: #3498db; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    sidebarHeaderLayout->addWidget(sbTitle);
    sidebarHeaderLayout->addStretch();
    
    sidebarHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sidebarHeader, &QWidget::customContextMenuRequested, this, [this, splitter, sidebarHeader](const QPoint& pos){
        QMenu menu;
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction(IconHelper::getIcon("nav_prev", "#aaaaaa", 18), "向左移动", [this, splitter](){
            int index = splitter->indexOf(m_sidebarContainer);
            if (index > 0) splitter->insertWidget(index - 1, m_sidebarContainer);
        });
        menu.addAction("向右移动", [this, splitter](){
            int index = splitter->indexOf(m_sidebarContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, m_sidebarContainer);
        });
        menu.exec(sidebarHeader->mapToGlobal(pos));
    });
    
    sidebarContainerLayout->addWidget(sidebarHeader);

    // 内容容器
    auto* sbContent = new QWidget();
    sbContent->setAttribute(Qt::WA_StyledBackground, true);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(8, 8, 8, 8);
    sbContentLayout->setSpacing(0);

    QString treeStyle = R"(
        QTreeView { background-color: transparent; border: none; color: #CCC; outline: none; }
        QTreeView::branch:has-children:closed { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMiAxMiI+CiAgPHBhdGggZD0iTTQgM2w0IDMtNCAzIiBmaWxsPSJub25lIiBzdHJva2U9IiNFRUVFRUUiIHN0cm9rZS13aWR0aD0iMiIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIi8+Cjwvc3ZnPg==); }
        QTreeView::branch:has-children:open   { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMiAxMiI+CiAgPHBhdGggZD0iTTMgNGwzIDQgMy00IiBmaWxsPSJub25lIiBzdHJva2U9IiNFRUVFRUUiIHN0cm9rZS13aWR0aD0iMiIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIi8+Cjwvc3ZnPg==); }
        QTreeView::item { height: 22px; padding-left: 10px; }
    )";

    m_systemTree = new DropTreeView();
    m_systemTree->setStyleSheet(treeStyle); 
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new CategoryModel(CategoryModel::System, this);
    m_systemTree->setModel(m_systemModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setRootIsDecorated(false);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); // 8 items * 22px = 176px
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new CategoryModel(CategoryModel::User, this);
    m_partitionTree->setModel(m_partitionModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setRootIsDecorated(true);
    m_partitionTree->setIndentation(16);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    m_partitionTree->expandAll();
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    sbContentLayout->addWidget(m_systemTree);
    sbContentLayout->addWidget(m_partitionTree);
    sidebarContainerLayout->addWidget(sbContent);

    // 直接放入 Splitter (移除 Wrapper)
    splitter->addWidget(m_sidebarContainer);

    auto onSidebarMenu = [this](const QPoint& pos){
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;
        
        QModelIndexList selected = tree->selectionModel()->selectedIndexes();
        QModelIndex index = tree->indexAt(pos);
        
        // 如果点击的项不在当前选中范围内，则切换选中为当前项
        if (index.isValid() && !selected.contains(index)) {
            tree->setCurrentIndex(index);
            selected.clear();
            selected << index;
        }

        QMenu menu(this);
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; color: white; }"); // 2026-03-13 修改悬停色为灰色，防止与蓝色图标视觉重合

        // [CRITICAL] 锁定：基于 NameRole 判定右键弹出逻辑，支持新建分类
        if (!index.isValid() || index.data(CategoryModel::NameRole).toString() == "我的分类") {
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
                FramelessInputDialog dlg("新建分类", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text);
                        refreshData();
                    }
                }
            });
            auto* importMenu = menu.addMenu(IconHelper::getIcon("file_import", "#1abc9c", 18), "导入数据");
            importMenu->setStyleSheet(menu.styleSheet());
            importMenu->addAction(IconHelper::getIcon("file", "#1abc9c", 18), "导入文件(s)...", [this]() {
                doImportCategory(-1);
            });
            importMenu->addAction(IconHelper::getIcon("folder", "#1abc9c", 18), "导入文件夹...", [this]() {
                doImportFolder(-1);
            });
            menu.exec(tree->mapToGlobal(pos));
            return;
        }

        QString type = index.data(CategoryModel::TypeRole).toString();
        QString idxName = index.data(CategoryModel::NameRole).toString();

        // 2026-03-22 [MODIFIED] 按照用户要求：支持特殊分类（全部、收藏、今日等）的导出菜单
        static const QStringList silentTypes = {"recently_visited", "untagged"};
        if (silentTypes.contains(type)) {
            return;
        }

        if (type == "all") {
            menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出完整结构数据", [this]() {
                if (!verifyExportPermission()) return;
                FileStorageHelper::exportFullStructure(this);
            });
            menu.exec(tree->mapToGlobal(pos));
            return;
        }

        if (type == "today" || type == "yesterday" || type == "bookmark") {
            menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), QString("导出 [%1]").arg(idxName), [this, type, idxName]() {
                if (!verifyExportPermission()) return;
                FileStorageHelper::exportByFilter(type, QVariant(), idxName, this);
            });
            menu.exec(tree->mapToGlobal(pos));
            return;
        }

        if (type == "category") {
            int catId = index.data(CategoryModel::IdRole).toInt();
            QString currentName = index.data(CategoryModel::NameRole).toString();

            // 2026-03-xx 按照用户要求：MainWindow 移除外部新建窗口，改为直接插入空笔记实现单窗口闭环
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
                int newId = DatabaseManager::instance().addNote("新记录", "", {}, "", catId);
                if (newId > 0) {
                    refreshData();
                    for (int i = 0; i < m_noteModel->rowCount(); ++i) {
                        QModelIndex idx = m_noteModel->index(i, 0);
                        if (idx.data(NoteModel::IdRole).toInt() == newId) {
                            m_noteList->setCurrentIndex(idx);
                            m_editor->setFocus();
                            break;
                        }
                    }
                }
            });
            menu.addAction(IconHelper::getIcon("branch", "#3498db", 18), "归类到此分类", [catId, currentName]() {
                DatabaseManager::instance().setExtensionTargetCategoryId(catId);
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #3498db;'>[OK] 已指定插件归类到: %1</b>").arg(currentName));
            });
            menu.addSeparator();
            auto* importMenu = menu.addMenu(IconHelper::getIcon("file_import", "#1abc9c", 18), "导入数据");
            importMenu->setStyleSheet(menu.styleSheet());
            importMenu->addAction(IconHelper::getIcon("file", "#1abc9c", 18), "导入文件(s)...", [this, catId]() {
                doImportCategory(catId);
            });
            importMenu->addAction(IconHelper::getIcon("folder", "#1abc9c", 18), "导入文件夹...", [this, catId]() {
                doImportFolder(catId);
            });

            // 2026-03-xx 按照用户要求：新增专属安装包 (.rnp) 导入，使用专用加密包图标
            importMenu->addAction(IconHelper::getIcon("package_rnp", "#9b59b6", 18), "导入专属安装包 (.rnp)", [this]() {
                FileStorageHelper::importFromPackage(this);
                this->refreshData();
            });
            
            // [任务1] 将右键菜单的“导出此分类”改为二级“导出”菜单
            auto* exportMenu = menu.addMenu(IconHelper::getIcon("file_export", "#3498db", 18), "导出");
            exportMenu->setStyleSheet(menu.styleSheet());
            
            QVariantMap rootCat = DatabaseManager::instance().getRootCategory(catId);
            QString rootName = rootCat.value("name").toString();
            int rootId = rootCat.value("id").toInt();
            
            if (rootId == catId) {
                // 2026-03-xx 按照用户最新要求：主分类菜单项仅执行单分类导出
                exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                    if (!verifyExportPermission()) return;
                    doExportCategory(rootId, rootName);
                });
                
                auto children = DatabaseManager::instance().getChildCategories(rootId);
                for (const auto& child : std::as_const(children)) {
                    int childId = child.value("id").toInt();
                    QString childName = child.value("name").toString();
                    exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), childName, [this, childId, childName]() {
                        if (!verifyExportPermission()) return;
                        doExportCategory(childId, childName);
                    });
                }
            } else {
                // 2026-03-xx 按照用户要求：子选项显示名称且仅执行单分类导出
                exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), currentName, [this, catId, currentName]() {
                    if (!verifyExportPermission()) return;
                    doExportCategory(catId, currentName);
                });
                if (!rootName.isEmpty()) {
                    exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                        if (!verifyExportPermission()) return;
                        doExportCategory(rootId, rootName);
                    });
                }
            }
            
            // 2026-03-xx 新增“整分类”选项，执行包含主分类及子分类的递归导出
            exportMenu->addSeparator();
            exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), "整分类", [this, rootId, rootName]() {
                if (!verifyExportPermission()) return;
                FileStorageHelper::exportCategoryRecursive(rootId, rootName, this);
            });

            // 2026-03-xx 按照用户要求：新增专属安装包 (.rnp) 导出，使用专用加密包图标
            exportMenu->addAction(IconHelper::getIcon("package_rnp", "#9b59b6", 18), "导出为数据包 (.rnp)", [this, catId, currentName]() {
                if (!verifyExportPermission()) return;
                FileStorageHelper::exportToPackage(catId, currentName, this);
            });
            
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("palette", "#e67e22", 18), "设置颜色", [this, catId]() {
                auto* dlg = new QColorDialog(Qt::gray, this);
                dlg->setWindowTitle("选择分类颜色");
                dlg->setWindowFlags(dlg->windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
                connect(dlg, &QColorDialog::colorSelected, [this, catId](const QColor& color){
                    if (color.isValid()) {
                        DatabaseManager::instance().setCategoryColor(catId, color.name());
                        refreshData();
                    }
                });
                connect(dlg, &QColorDialog::finished, dlg, &QObject::deleteLater);
                dlg->show();
            });
            menu.addAction(IconHelper::getIcon("random_color", "#FF6B9D", 18), "随机颜色", [this, catId]() {
                static const QStringList palette = {
                    "#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEEAD",
                    "#D4A5A5", "#9B59B6", "#3498DB", "#E67E22", "#2ECC71",
                    "#E74C3C", "#F1C40F", "#1ABC9C", "#34495E", "#95A5A6"
                };
                QString chosenColor = palette.at(QRandomGenerator::global()->bounded(palette.size()));
                DatabaseManager::instance().setCategoryColor(catId, chosenColor);
                refreshData();
            });
            menu.addAction(IconHelper::getIcon("tag", "#FFAB91", 18), "设置预设标签", [this, catId]() {
                QString currentTags = DatabaseManager::instance().getCategoryPresetTags(catId);
                FramelessInputDialog dlg("设置预设标签", "标签 (逗号分隔):", currentTags, this);
                if (dlg.exec() == QDialog::Accepted) {
                    DatabaseManager::instance().setCategoryPresetTags(catId, dlg.text());
                }
            });
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建分类", [this]() {
                FramelessInputDialog dlg("新建分类", "组名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text);
                        refreshData();
                    }
                }
            });
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分类", [this, catId]() {
                FramelessInputDialog dlg("新建子分类", "区名称:", "", this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString text = dlg.text();
                    if (!text.isEmpty()) {
                        DatabaseManager::instance().addCategory(text, catId);
                        refreshData();
                    }
                }
            });
            menu.addSeparator();

            if (selected.size() == 1) {
                bool isPinned = index.data(CategoryModel::PinnedRole).toBool();
                // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色 (#FF551C)
                menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18), 
                               isPinned ? "取消置顶" : "置顶分类", [this, catId]() {
                    DatabaseManager::instance().toggleCategoryPinned(catId);
                    // MainWindow 的侧边栏刷新逻辑通常集成在 refreshData 或通过信号触发，
                    // 但为了保险，我们在这里显式处理或确认模型刷新。
                    refreshData(); 
                });
                
                menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名分类", [this, index]() {
                    m_partitionTree->edit(index);
                });
            }

            QString deleteText = selected.size() > 1 ? QString("删除选中的 %1 个分类").arg(selected.size()) : "删除分类";
            menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), deleteText, [this, selected]() {
                // 2026-03-xx 按照用户要求：改为混合删除（分类物理删除，内容移至回收站）
                QString confirmMsg = selected.size() > 1 ? "确定要删除选中的分类吗？\n(分类将被永久抹除，其中的笔记将移至回收站)" : "确定要删除此分类吗？\n(分类将永久消失，其内容将移至回收站)";
                FramelessMessageBox dlg("确认删除", confirmMsg, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QList<int> ids;
                    for (const auto& idx : selected) {
                        if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                            ids << idx.data(CategoryModel::IdRole).toInt();
                        }
                    }
                    qDebug() << "[MainWindow] 准备物理删除分类，提取到的 IDs:" << ids;
                    DatabaseManager::instance().hardDeleteCategories(ids);
                    refreshData();
                }
            });

            menu.addSeparator();
            auto* sortMenu = menu.addMenu(IconHelper::getIcon("list_ol", "#aaaaaa", 18), "排列");
            sortMenu->setStyleSheet(menu.styleSheet());

            int parentId = -1;
            QModelIndex parentIdx = index.parent();
            if (parentIdx.isValid() && parentIdx.data(CategoryModel::TypeRole).toString() == "category") {
                parentId = parentIdx.data(CategoryModel::IdRole).toInt();
            }

            sortMenu->addAction("标题(当前层级) (A→Z)", [this, parentId]() {
                if (DatabaseManager::instance().reorderCategories(parentId, true))
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 排列已完成</b>");
            });
            sortMenu->addAction("标题(当前层级) (Z→A)", [this, parentId]() {
                if (DatabaseManager::instance().reorderCategories(parentId, false))
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 排列已完成</b>");
            });
            sortMenu->addAction("标题(全部) (A→Z)", [this]() {
                if (DatabaseManager::instance().reorderAllCategories(true))
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部排列已完成</b>");
            });
            sortMenu->addAction("标题(全部) (Z→A)", [this]() {
                if (DatabaseManager::instance().reorderAllCategories(false))
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部排列已完成</b>");
            });

            menu.addSeparator();
            auto* pwdMenu = menu.addMenu(IconHelper::getIcon("lock", "#aaaaaa", 18), "密码保护");
            pwdMenu->setStyleSheet(menu.styleSheet());
            
            pwdMenu->addAction("设置", [this, catId]() {
                QTimer::singleShot(0, [this, catId]() {
                    CategoryPasswordDialog dlg("设置密码", this);
                    if (dlg.exec() == QDialog::Accepted) {
                        DatabaseManager::instance().setCategoryPassword(catId, dlg.password(), dlg.passwordHint());
                        refreshData();
                    }
                });
            });
            pwdMenu->addAction("修改", [this, catId]() {
                QTimer::singleShot(0, [this, catId]() {
                    FramelessInputDialog verifyDlg("验证旧密码", "请输入当前密码:", "", this);
                    verifyDlg.setEchoMode(QLineEdit::Password);
                    if (verifyDlg.exec() == QDialog::Accepted) {
                        if (DatabaseManager::instance().verifyCategoryPassword(catId, verifyDlg.text())) {
                            CategoryPasswordDialog dlg("修改密码", this);
                            QString currentHint;
                            auto cats = DatabaseManager::instance().getAllCategories();
                            for(const auto& c : std::as_const(cats)) if(c.value("id").toInt() == catId) currentHint = c.value("password_hint").toString();
                            dlg.setInitialData(currentHint);
                            if (dlg.exec() == QDialog::Accepted) {
                                DatabaseManager::instance().setCategoryPassword(catId, dlg.password(), dlg.passwordHint());
                                refreshData();
                            }
                        } else {
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 旧密码验证失败</b>");
                        }
                    }
                });
            });
            pwdMenu->addAction("移除", [this, catId]() {
                QTimer::singleShot(0, [this, catId]() {
                    FramelessInputDialog dlg("验证密码", "请输入当前密码以移除保护:", "", this);
                    dlg.setEchoMode(QLineEdit::Password);
                    if (dlg.exec() == QDialog::Accepted) {
                        if (DatabaseManager::instance().verifyCategoryPassword(catId, dlg.text())) {
                            DatabaseManager::instance().removeCategoryPassword(catId);
                            refreshData();
                        } else {
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密码错误</b>");
                        }
                    }
                });
            });
            pwdMenu->addAction("立即锁定", [this, catId]() {
                DatabaseManager::instance().lockCategory(catId);
                refreshData();
            });
        } else if (idxName == "未分类" || type == "uncategorized") {
            // 2026-03-xx 按照用户要求：MainWindow 移除外部新建窗口
            menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", this, &MainWindow::doNewIdea);
            menu.addAction(IconHelper::getIcon("branch", "#3498db", 18), "归类到此分类", []() {
                DatabaseManager::instance().setExtensionTargetCategoryId(-1);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #3498db;'>[OK] 已指定插件归类到: 未分类</b>");
            });
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出 [未分类]", [this]() {
                if (!verifyExportPermission()) return;
                FileStorageHelper::exportByFilter("uncategorized", -1, "未分类灵感", this);
            });
        } else if (type == "trash") {
            menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复 (到未分类)", [this](){
                DatabaseManager::instance().restoreAllFromTrash();
                refreshData();
            });
            menu.addSeparator();
            menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "清空回收站", [this]() {
                FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
                if (dlg.exec() == QDialog::Accepted) {
                    DatabaseManager::instance().emptyTrash();
                    refreshData();
                }
            });
        }
        menu.exec(tree->mapToGlobal(pos));
    };

    connect(m_systemTree, &QTreeView::customContextMenuRequested, this, onSidebarMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, onSidebarMenu);

    auto onSelection = [this](QTreeView* tree, const QModelIndex& index) {
        if (!index.isValid()) return;
        if (tree == m_systemTree) {
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());
        } else {
            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
        }
        onTagSelected(index);
    };

    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelection](const QModelIndex& idx){ onSelection(m_partitionTree, idx); });
    
    // 连接拖拽信号 (使用 Model 定义的枚举)
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex){
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();
        for (int id : ids) {
            if (type == "category") {
                int catId = targetIndex.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().updateNoteState(id, "category_id", catId);
                ActionRecorder::instance().recordMoveToCategory(catId);
            } else if (targetIndex.data().toString() == "收藏" || type == "bookmark") { 
                DatabaseManager::instance().updateNoteState(id, "is_favorite", 1);
            } else if (type == "trash") {
                DatabaseManager::instance().updateNoteState(id, "is_deleted", 1);
            } else if (type == "uncategorized") {
                DatabaseManager::instance().updateNoteState(id, "category_id", QVariant());
                ActionRecorder::instance().recordMoveToCategory(-1);
            }
        }
        refreshData();
    };

    connect(m_systemTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_partitionTree->isExpanded(index)) m_partitionTree->collapse(index);
        else m_partitionTree->expand(index);
    });

    // 3. 中间列表卡片容器
    auto* listContainer = new QFrame();
    listContainer->setMinimumWidth(230); // 对齐 MetadataPanel
    listContainer->setObjectName("ListContainer");
    listContainer->setAttribute(Qt::WA_StyledBackground, true);
    listContainer->setStyleSheet(
        "#ListContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* listShadow = new QGraphicsDropShadowEffect(listContainer);
    listShadow->setBlurRadius(10);
    listShadow->setXOffset(0);
    listShadow->setYOffset(4);
    listShadow->setColor(QColor(0, 0, 0, 150));
    listContainer->setGraphicsEffect(listShadow);

    auto* listContainerLayout = new QVBoxLayout(listContainer);
    listContainerLayout->setContentsMargins(0, 0, 0, 0); 
    listContainerLayout->setSpacing(0);

    m_listFocusLine = new QWidget();
    m_listFocusLine->setFixedHeight(1);
    m_listFocusLine->setStyleSheet("background-color: #2ecc71;");
    m_listFocusLine->hide();
    listContainerLayout->addWidget(m_listFocusLine);

    // 列表标题栏 (锁定 32px, 统一配色与分割线)
    auto* listHeader = new QWidget();
    listHeader->setFixedHeight(32);
    listHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;" 
    );
    auto* listHeaderLayout = new QHBoxLayout(listHeader);
    listHeaderLayout->setContentsMargins(15, 0, 15, 0); 
    auto* listIcon = new QLabel();
    listIcon->setPixmap(IconHelper::getIcon("list_ul", "#2ecc71").pixmap(18, 18));
    listHeaderLayout->addWidget(listIcon);
    auto* listHeaderTitle = new QLabel("导航");
    listHeaderTitle->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    listHeaderLayout->addWidget(listHeaderTitle);
    listHeaderLayout->addStretch();
    
    listHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listHeader, &QWidget::customContextMenuRequested, this, [this, listContainer, splitter, listHeader](const QPoint& pos){
        QMenu menu;
        IconHelper::setupMenu(&menu);
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, listContainer, splitter](){
            int index = splitter->indexOf(listContainer);
            if (index > 0) splitter->insertWidget(index - 1, listContainer);
        });
        menu.addAction("向右移动", [this, listContainer, splitter](){
            int index = splitter->indexOf(listContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, listContainer);
        });
        menu.exec(listHeader->mapToGlobal(pos));
    });
    
    listContainerLayout->addWidget(listHeader);

    // 内容容器
    auto* listContent = new QWidget();
    listContent->setAttribute(Qt::WA_StyledBackground, true);
    listContent->setStyleSheet("background: transparent; border: none;");
    auto* listContentLayout = new QVBoxLayout(listContent);
    // 恢复垂直边距为 8，保留水平边距 15 以对齐宽度
    listContentLayout->setContentsMargins(15, 8, 15, 8);
    
    m_noteList = new CleanListView();
    m_noteList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noteList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noteModel = new NoteModel(this);
    m_noteList->setModel(m_noteModel);
    m_noteList->setItemDelegate(new NoteDelegate(m_noteList));
    m_noteList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_noteList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_noteList, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    
    // 恢复垂直间距为 5，垂直 Padding 为 5；仅水平 Padding 设为 0
    m_noteList->setSpacing(5); 
    m_noteList->setStyleSheet("QListView { background: transparent; border: none; padding-top: 5px; padding-bottom: 5px; padding-left: 0px; padding-right: 0px; }");
    
    // 基础拖拽使能 (其余复杂逻辑已由 CleanListView 实现)
    m_noteList->setDragEnabled(true);
    m_noteList->setAcceptDrops(true);
    m_noteList->setDropIndicatorShown(true);
    
    auto* cleanListView = qobject_cast<CleanListView*>(m_noteList);
    if (cleanListView) {
        connect(cleanListView, &CleanListView::internalMoveRequested, this, [this](const QList<int>& ids, int row){
            if (m_currentFilterType == "recently_visited" || m_currentFilterType == "trash") {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 当前视图不支持手动排序</b>");
                return;
            }
            DatabaseManager::instance().moveNotesToRow(ids, row, m_currentFilterType, m_currentFilterValue, m_filterPanel->getCheckedCriteria());
        });
    }

    connect(m_noteList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_noteList, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        if (!index.isValid()) return;
        int id = index.data(NoteModel::IdRole).toInt();
        // [CRITICAL] 锁定：双击视为实际操作，必须显式记录访问。严禁移除。
        DatabaseManager::instance().recordAccess(id); 
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        QString type = note.value("item_type").toString();
        
        QString plainContent = StringUtils::htmlToPlainText(note.value("content").toString()).trimmed();
        bool isExplicitPath = (type == "local_file" || type == "local_folder" || type == "local_batch");
        bool isAbsoluteTextPath = (!isExplicitPath && QFileInfo(plainContent).exists() && QFileInfo(plainContent).isAbsolute());

        // [CRITICAL] 锁定：双击智能打开逻辑。
        if (isExplicitPath || isAbsoluteTextPath) {
            QString path = isExplicitPath ? note.value("content").toString() : plainContent;
            QString fullPath = path;
            if (path.startsWith("attachments/")) {
                fullPath = QCoreApplication::applicationDirPath() + "/" + path;
            }
            
            if (QFileInfo::exists(fullPath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 文件已丢失：<br></b>" + fullPath);
            }
            return;
        }

        // 2026-03-xx 按照用户要求：RapidManager 移除所有外部编辑窗口，双击直接让内部编辑器获焦
        m_editor->setFocus();
    });

    listContentLayout->addWidget(m_noteList);

    m_lockWidget = new CategoryLockWidget(this);
    m_lockWidget->setVisible(false);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
    });
    connect(m_lockWidget, &CategoryLockWidget::escPressed, this, [this](){
        this->setFocus();
    });
    listContentLayout->addWidget(m_lockWidget);

    listContainerLayout->addWidget(listContent);
    splitter->addWidget(listContainer);
    
    // 4. 编辑器容器 (Card) - 独立出来
    auto* editorContainer = new QFrame();
    editorContainer->setMinimumWidth(230);
    editorContainer->setObjectName("EditorContainer");
    editorContainer->setAttribute(Qt::WA_StyledBackground, true);
    editorContainer->setStyleSheet(
        "#EditorContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* editorShadow = new QGraphicsDropShadowEffect(editorContainer);
    editorShadow->setBlurRadius(10);
    editorShadow->setXOffset(0);
    editorShadow->setYOffset(4);
    editorShadow->setColor(QColor(0, 0, 0, 150));
    editorContainer->setGraphicsEffect(editorShadow);

    auto* editorContainerLayout = new QVBoxLayout(editorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    editorContainerLayout->setSpacing(0);

    // 编辑器标题栏 (全宽贯穿线)
    auto* editorHeader = new QWidget();
    editorHeader->setFixedHeight(32);
    editorHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    // [CRITICAL] 视觉对齐锁定：此处顶部边距必须设为 2px，以配合 32px 的标题栏高度，使文字达到垂直居中。
    editorHeaderLayout->setContentsMargins(15, 2, 15, 0);
    auto* edIcon = new QLabel();
    // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
    edIcon->setPixmap(IconHelper::getIcon("eye", "#41F2F2").pixmap(18, 18));
    editorHeaderLayout->addWidget(edIcon);
    auto* edTitle = new QLabel("内容（文件夹 / 文件）"); // 保护用户修改的标题内容
    edTitle->setStyleSheet("color: #41F2F2; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    editorHeaderLayout->addWidget(edTitle);
    editorHeaderLayout->addStretch();

    // [CRITICAL] 编辑逻辑重定义：2026-03-xx 按照用户要求，独立程序 (RapidManager) 采用单窗口闭环，激活内置 Editor 编辑功能。
    m_editBtn = new QPushButton();
    m_editBtn->setFixedSize(24, 24);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setEnabled(false);
    m_editBtn->setProperty("tooltipText", "保存修改 (Ctrl+S)"); m_editBtn->installEventFilter(this);
    m_editBtn->setIcon(IconHelper::getIcon("save", "#555555"));
    m_editBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover:enabled { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_editBtn, &QPushButton::clicked, this, [this](){
        // 2026-03-xx 按照用户要求：既然是单窗口化，编辑后的保存直接调用内部保存逻辑
        QModelIndex index = m_noteList->currentIndex();
        if (!index.isValid()) return;

        int id = index.data(NoteModel::IdRole).toInt();
        QString content = m_editor->getOptimizedContent();

        // 简单更新内容，保留其余元数据
        if (DatabaseManager::instance().updateNoteState(id, "content", content)) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 内容已保存</b>", 700);
            refreshData();
        }
    });
    editorHeaderLayout->addWidget(m_editBtn);

    editorHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editorHeader, &QWidget::customContextMenuRequested, this, [this, editorContainer, splitter, editorHeader](const QPoint& pos){
        QMenu menu;
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, editorContainer, splitter](){
            int index = splitter->indexOf(editorContainer);
            if (index > 0) splitter->insertWidget(index - 1, editorContainer);
        });
        menu.addAction("向右移动", [this, editorContainer, splitter](){
            int index = splitter->indexOf(editorContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, editorContainer);
        });
        menu.exec(editorHeader->mapToGlobal(pos));
    });

    editorContainerLayout->addWidget(editorHeader);

    // 内容容器
    auto* editorContent = new QWidget();
    editorContent->setAttribute(Qt::WA_StyledBackground, true);
    editorContent->setStyleSheet("background: transparent; border: none;");
    auto* editorContentLayout = new QVBoxLayout(editorContent);
    editorContentLayout->setContentsMargins(2, 2, 2, 2); // 编辑器保留微量对齐边距

    m_editor = new Editor();
    // 2026-03-xx 按照用户要求：RapidManager 采用实时编辑模式，取消只读限制
    m_editor->togglePreview(false);
    m_editor->setReadOnly(false);
    
    editorContentLayout->addWidget(m_editor);
    editorContainerLayout->addWidget(editorContent);
    
    // 直接放入 Splitter
    splitter->addWidget(editorContainer);

    // 5. 元数据面板 - 独立出来
    m_metaPanel = new MetadataPanel(this);
    m_metaPanel->setMinimumWidth(230);
    connect(m_metaPanel, &MetadataPanel::noteUpdated, this, &MainWindow::refreshData);
    connect(m_metaPanel, &MetadataPanel::closed, this, [this](){
        m_header->setMetadataActive(false);
    });
    connect(m_metaPanel, &MetadataPanel::tagAdded, this, [this](const QStringList& tags){
        QModelIndexList indices = m_noteList->selectionModel()->selectedIndexes();
        if (indices.isEmpty()) return;
        for (const auto& index : std::as_const(indices)) {
            int id = index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().addTagsToNote(id, tags);
        }
        refreshData();
    });
    
    // 给元数据面板添加右键移动菜单
    auto* metaHeader = m_metaPanel->findChild<QWidget*>("MetadataHeader");
    if (metaHeader) {
        metaHeader->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(metaHeader, &QWidget::customContextMenuRequested, this, [this, splitter, metaHeader](const QPoint& pos){
            QMenu menu;
            IconHelper::setupMenu(&menu);
            menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                               /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                               "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                               "QMenu::icon { margin-left: 6px; } "
                               "QMenu::item:selected { background-color: #3E3E42; }");
            menu.addAction("向左移动", [this, splitter](){
                int index = splitter->indexOf(m_metaPanel);
                if (index > 0) splitter->insertWidget(index - 1, m_metaPanel);
            });
            menu.addAction("向右移动", [this, splitter](){
                int index = splitter->indexOf(m_metaPanel);
                if (index < splitter->count() - 1) splitter->insertWidget(index + 1, m_metaPanel);
            });
            menu.exec(metaHeader->mapToGlobal(pos));
        });
    }

    splitter->addWidget(m_metaPanel);
    
    // [CRITICAL] 为元数据面板的输入框安装事件过滤器
    if (m_metaPanel) {
        if (m_metaPanel->m_tagEdit) m_metaPanel->m_tagEdit->installEventFilter(this);
    }

    // 6. 筛选器器卡片容器
    auto* filterContainer = new QFrame();
    filterContainer->setMinimumWidth(230);
    filterContainer->setObjectName("FilterContainer");
    filterContainer->setAttribute(Qt::WA_StyledBackground, true);
    filterContainer->setStyleSheet(
        "#FilterContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-top-left-radius: 0px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
    );

    auto* filterShadow = new QGraphicsDropShadowEffect(filterContainer);
    filterShadow->setBlurRadius(10);
    filterShadow->setXOffset(0);
    filterShadow->setYOffset(4);
    filterShadow->setColor(QColor(0, 0, 0, 150));
    filterContainer->setGraphicsEffect(filterShadow);

    auto* filterContainerLayout = new QVBoxLayout(filterContainer);
    filterContainerLayout->setContentsMargins(0, 0, 0, 0);
    filterContainerLayout->setSpacing(0);

    // 筛选器标题栏
    auto* filterHeader = new QWidget();
    filterHeader->setFixedHeight(32);
    filterHeader->setStyleSheet(
        "background-color: #252526; "
        "border-top-left-radius: 0px; "
        "border-top-right-radius: 0px; "
        "border-bottom: 1px solid #333;"
    );
    auto* filterHeaderLayout = new QHBoxLayout(filterHeader);
    filterHeaderLayout->setContentsMargins(15, 0, 4, 0);
    auto* fiIcon = new QLabel();
    fiIcon->setPixmap(IconHelper::getIcon("filter", "#f1c40f").pixmap(18, 18));
    filterHeaderLayout->addWidget(fiIcon);
    auto* fiTitle = new QLabel("筛选器");
    fiTitle->setStyleSheet("color: #f1c40f; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    filterHeaderLayout->addWidget(fiTitle);
    filterHeaderLayout->addStretch();

    auto* filterCloseBtn = new QPushButton();
    filterCloseBtn->setIcon(IconHelper::getIcon("close", "#888888"));
    filterCloseBtn->setFixedSize(24, 24);
    filterCloseBtn->setCursor(Qt::PointingHandCursor);
    filterCloseBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    connect(filterCloseBtn, &QPushButton::clicked, this, [this](){
        m_filterWrapper->hide();
        m_header->setFilterActive(false);
    });
    filterHeaderLayout->addWidget(filterCloseBtn);
    
    filterHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filterHeader, &QWidget::customContextMenuRequested, this, [this, filterContainer, splitter, filterHeader](const QPoint& pos){
        QMenu menu;
        menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
        menu.addAction("向左移动", [this, filterContainer, splitter](){
            int index = splitter->indexOf(filterContainer);
            if (index > 0) splitter->insertWidget(index - 1, filterContainer);
        });
        menu.addAction("向右移动", [this, filterContainer, splitter](){
            int index = splitter->indexOf(filterContainer);
            if (index < splitter->count() - 1) splitter->insertWidget(index + 1, filterContainer);
        });
        menu.exec(filterHeader->mapToGlobal(pos));
    });
    
    filterContainerLayout->addWidget(filterHeader);

    // 内容容器
    auto* filterContent = new QWidget();
    filterContent->setAttribute(Qt::WA_StyledBackground, true);
    filterContent->setStyleSheet("background: transparent; border: none;");
    auto* filterContentLayout = new QVBoxLayout(filterContent);
    filterContentLayout->setContentsMargins(0, 0, 10, 10);

    m_filterPanel = new FilterPanel(this);
    m_filterPanel->setStyleSheet("background: transparent; border: none;");
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::refreshData);
    filterContentLayout->addWidget(m_filterPanel);
    filterContainerLayout->addWidget(filterContent);

    m_filterWrapper = filterContainer;
    splitter->addWidget(m_filterWrapper);

    // 2026-03-13 修复逻辑：监听 Splitter 移动，实时更新焦点线状态
    connect(splitter, &QSplitter::splitterMoved, this, &MainWindow::updateFocusLines);

    splitter->setStretchFactor(0, 1); 
    splitter->setStretchFactor(1, 2); 
    splitter->setStretchFactor(2, 8); 
    splitter->setStretchFactor(3, 1); 
    splitter->setStretchFactor(4, 1);
    
    // 显式设置初始大小比例
    splitter->setSizes({230, 230, 600, 230, 230});

    contentLayout->addWidget(splitter);
    mainLayout->addWidget(contentWidget);

    m_systemTree->installEventFilter(this);
    m_partitionTree->installEventFilter(this);
    if (m_header) {
        if (m_header->searchEdit()) m_header->searchEdit()->installEventFilter(this);
        if (m_header->pageInput()) m_header->pageInput()->installEventFilter(this);
    }


    m_noteList->installEventFilter(this);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();

    // [CRITICAL] 拦截内部拖拽逻辑：如果数据包含应用内部笔记 ID，说明是列表内的移动操作，
    // 严禁触发外部导入/新建笔记逻辑，从而彻底根除因拖拽导致的数据重复创建问题。
    if (mime->hasFormat("application/x-note-ids")) {
        event->ignore();
        return;
    }

    int targetId = -1;
    if (m_currentFilterType == "category") {
        targetId = m_currentFilterValue.toInt();
    }

    QStringList localPaths = StringUtils::extractLocalPathsFromMime(mime);
    if (!localPaths.isEmpty()) {
        FileStorageHelper::processImport(localPaths, targetId);
        event->acceptProposedAction();
        return;
    }

    if (mime->hasUrls()) {
        QList<QUrl> urls = mime->urls();
        QStringList remoteUrls;
        for (const QUrl& url : std::as_const(urls)) {
            if (!url.isLocalFile() && !url.toString().startsWith("file:///")) {
                remoteUrls << url.toString();
            }
        }

        if (!remoteUrls.isEmpty()) {
            DatabaseManager::instance().addNote("外部链接", remoteUrls.join(";"), {"链接"}, "", targetId, "link");
            event->acceptProposedAction();
            return;
        }
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        QString content = mime->text();
        QString title = content.trimmed().left(50).replace("\n", " ");
        DatabaseManager::instance().addNote(title, content, {}, "", targetId, "text");
        event->acceptProposedAction();
    } else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QByteArray dataBlob;
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            DatabaseManager::instance().addNote("[拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), "[Image Data]", {}, "", targetId, "image", dataBlob);
            event->acceptProposedAction();
        }
    }
}

bool MainWindow::event(QEvent* event) {
    // 2026-03-xx 按照用户要求：管理端 MainWindow 独立运行，移除对全局热键管理器的依赖
    return QMainWindow::event(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);

    // [USER_REQUEST] 按照用户要求：只要启动后，焦点自动锁定在列表，不可锁定在搜索数据的搜索框
    if (m_noteList) {
        m_noteList->setFocus();
        if (m_noteModel && m_noteModel->rowCount() > 0 && !m_noteList->currentIndex().isValid()) {
            m_noteList->setCurrentIndex(m_noteModel->index(0, 0));
        }
    }

    // 从 HeaderBar 获取按钮状态
    if (m_header) {
        auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
        if (btn && btn->isChecked()) {
#ifdef Q_OS_WIN
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
            Qt::WindowFlags f = windowFlags();
            f |= Qt::WindowStaysOnTopHint;
            setWindowFlags(f);
            show();
#endif
        }
    }
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);

    // [NEW] 拦截背景擦除，防止缩放闪烁
    if (msg->message == WM_ERASEBKGND) {
        *result = 1;
        return true;
    }

    // [NEW] 拦截 NCCALCSIZE，确保内容填充整个窗口并减少抖动
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        *result = 0;
        return true;
    }

    if (msg->message == WM_NCHITTEST) {
        int x = GET_X_LPARAM(msg->lParam);
        int y = GET_Y_LPARAM(msg->lParam);
        
        QPoint pos = mapFromGlobal(QPoint(x, y));
        int margin = RESIZE_MARGIN;
        int w = width();
        int h = height();

        bool left = pos.x() < margin;
        bool right = pos.x() > w - margin;
        bool top = pos.y() < margin;
        bool bottom = pos.y() > h - margin;

        if (top && left) *result = HTTOPLEFT;
        else if (top && right) *result = HTTOPRIGHT;
        else if (bottom && left) *result = HTBOTTOMLEFT;
        else if (bottom && right) *result = HTBOTTOMRIGHT;
        else if (top) *result = HTTOP;
        else if (bottom) *result = HTBOTTOM;
        else if (left) *result = HTLEFT;
        else if (right) *result = HTRIGHT;
        else return QMainWindow::nativeEvent(eventType, message, result);

        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::onNoteAdded(const QVariantMap& note) {
    // 1. 基础状态检查
    if (note.value("is_deleted").toInt() == 1) return;

    // 2. 检查分类/状态过滤器匹配
    bool matches = true;
    if (m_currentFilterType == "category") {
        matches = (note.value("category_id").toInt() == m_currentFilterValue.toInt());
    } else if (m_currentFilterType == "untagged") {
        matches = note.value("tags").toString().isEmpty();
    } else if (m_currentFilterType == "bookmark") {
        matches = (note.value("is_favorite").toInt() == 1);
    } else if (m_currentFilterType == "trash") {
        matches = false;
    } else if (m_currentFilterType == "recently_visited") {
        matches = false; // 新笔记尚未正式被“访问”
    }

    // 3. 关键词匹配检查
    if (matches && !m_currentKeyword.isEmpty()) {
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QString tags = note.value("tags").toString();
        if (!title.contains(m_currentKeyword, Qt::CaseInsensitive) && 
            !content.contains(m_currentKeyword, Qt::CaseInsensitive) && 
            !tags.contains(m_currentKeyword, Qt::CaseInsensitive)) {
            matches = false;
        }

    }

    // 4. 筛选器器活跃时，为了保证精准，采取全量刷新策略
    if (matches && !m_filterWrapper->isHidden()) {
        matches = false;
    }

    if (matches && m_currentPage == 1) {
        m_noteModel->prependNote(note);
        m_noteList->scrollToTop();
    }
    
    // 依然需要触发侧边栏计数同步与潜在的筛选器状态刷新
    scheduleRefresh();
}

void MainWindow::scheduleRefresh() {
    m_refreshTimer->start();
}

void MainWindow::refreshData() {
    qDebug() << "[MainWindow] 开始执行 refreshData()...";
    // 保存当前选中项状态以供恢复
    QString selectedType;
    QVariant selectedValue;
    QModelIndex sysIdx = m_systemTree->currentIndex();
    QModelIndex partIdx = m_partitionTree->currentIndex();
    
    // 记忆当前选中的笔记 ID 列表，以便在刷新后恢复多选状态
    QSet<int> selectedNoteIds;
    auto selectedIndices = m_noteList->selectionModel()->selectedIndexes();
    for (const auto& idx : selectedIndices) {
        selectedNoteIds.insert(idx.data(NoteModel::IdRole).toInt());
    }
    int lastCurrentNoteId = m_noteList->currentIndex().data(NoteModel::IdRole).toInt();

    if (sysIdx.isValid()) {
        selectedType = sysIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = sysIdx.data(CategoryModel::NameRole);
    } else if (partIdx.isValid()) {
        selectedType = partIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = partIdx.data(CategoryModel::IdRole);
    }

    QSet<QString> expandedPaths;
    std::function<void(const QModelIndex&)> checkChildren = [&](const QModelIndex& parent) {
        for (int j = 0; j < m_partitionModel->rowCount(parent); ++j) {
            QModelIndex child = m_partitionModel->index(j, 0, parent);
            if (m_partitionTree->isExpanded(child)) {
                QString type = child.data(CategoryModel::TypeRole).toString();
                if (type == "category") {
                    expandedPaths.insert("cat_" + QString::number(child.data(CategoryModel::IdRole).toInt()));
                } else {
                    expandedPaths.insert(child.data(CategoryModel::NameRole).toString());
                }
            }
            if (m_partitionModel->rowCount(child) > 0) checkChildren(child);
        }
    };

    for (int i = 0; i < m_partitionModel->rowCount(); ++i) {
        QModelIndex index = m_partitionModel->index(i, 0);
        if (m_partitionTree->isExpanded(index)) {
            expandedPaths.insert(index.data(CategoryModel::NameRole).toString());
        }
        checkChildren(index);
    }

    QVariantMap criteria = m_filterPanel->getCheckedCriteria();
    auto notes = DatabaseManager::instance().searchNotes(m_currentKeyword, m_currentFilterType, m_currentFilterValue, m_currentPage, m_pageSize, criteria);
    int totalCount = DatabaseManager::instance().getNotesCount(m_currentKeyword, m_currentFilterType, m_currentFilterValue, criteria);

    // 检查当前分类是否锁定
    bool isLocked = false;
    if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
        int catId = m_currentFilterValue.toInt();
        if (DatabaseManager::instance().isCategoryLocked(catId)) {
            isLocked = true;
            QString hint;
            auto cats = DatabaseManager::instance().getAllCategories();
            for(const auto& c : std::as_const(cats)) if(c.value("id").toInt() == catId) hint = c.value("password_hint").toString();
            m_lockWidget->setCategory(catId, hint);
        }
    }

    m_noteList->setVisible(!isLocked);
    m_lockWidget->setVisible(isLocked);

    if (isLocked) {
        m_editor->setPlainText("");
        m_metaPanel->clearSelection();
    }

    m_noteModel->setNotes(isLocked ? QList<QVariantMap>() : notes);

    // 恢复笔记选中状态 (支持多选恢复)
    if (!selectedNoteIds.isEmpty()) {
        QItemSelection selection;
        for (int i = 0; i < m_noteModel->rowCount(); ++i) {
            QModelIndex idx = m_noteModel->index(i, 0);
            int id = idx.data(NoteModel::IdRole).toInt();
            if (selectedNoteIds.contains(id)) {
                selection.select(idx, idx);
            }
            if (id == lastCurrentNoteId) {
                m_noteList->setCurrentIndex(idx);
            }
        }
        if (!selection.isEmpty()) {
            m_noteList->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();

    int totalPages = (totalCount + m_pageSize - 1) / m_pageSize;
    if (totalPages < 1) totalPages = 1;
    m_header->updatePagination(m_currentPage, totalPages);

    // 恢复系统项选中
    if (!selectedType.isEmpty() && selectedType != "category") {
        for (int i = 0; i < m_systemModel->rowCount(); ++i) {
            QModelIndex idx = m_systemModel->index(i, 0);
            if (idx.data(CategoryModel::TypeRole).toString() == selectedType &&
                idx.data(CategoryModel::NameRole) == selectedValue) {
                m_systemTree->setCurrentIndex(idx);
                break;
            }
        }
    }

    // 恢复分类选中与展开
    for (int i = 0; i < m_partitionModel->rowCount(); ++i) {
        QModelIndex index = m_partitionModel->index(i, 0);
        QString name = index.data(CategoryModel::NameRole).toString();

        // [CRITICAL] 锁定：基于 NameRole 恢复默认展开状态
        if (name == "我的分类" || expandedPaths.contains(name)) {
            m_partitionTree->setExpanded(index, true);
        }
        
        std::function<void(const QModelIndex&)> restoreChildren = [&](const QModelIndex& parent) {
            for (int j = 0; j < m_partitionModel->rowCount(parent); ++j) {
                QModelIndex child = m_partitionModel->index(j, 0, parent);
                QString cType = child.data(CategoryModel::TypeRole).toString();
                QString cName = child.data(CategoryModel::NameRole).toString();
                
                // 恢复选中
                if (!selectedType.isEmpty() && cType == "category" && child.data(CategoryModel::IdRole) == selectedValue) {
                    m_partitionTree->setCurrentIndex(child);
                }

                QString identifier = (cType == "category") ? 
                    ("cat_" + QString::number(child.data(CategoryModel::IdRole).toInt())) : cName;

                // [CRITICAL] 锁定：确保“我的分类”下的直属分类始终展开
                if (expandedPaths.contains(identifier) || (parent.data(CategoryModel::NameRole).toString() == "我的分类")) {
                    m_partitionTree->setExpanded(child, true);
                }
                if (m_partitionModel->rowCount(child) > 0) restoreChildren(child);
            }
        };
        restoreChildren(index);
    }

    if (!m_filterWrapper->isHidden()) {
        m_filterPanel->updateStats(m_currentKeyword, m_currentFilterType, m_currentFilterValue);
    }
}

void MainWindow::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QModelIndexList indices = m_noteList->selectionModel()->selectedIndexes();
    if (indices.isEmpty()) {
        m_metaPanel->clearSelection();
        m_editor->setPlainText("");
        m_editBtn->setEnabled(false);
        m_editBtn->setIcon(IconHelper::getIcon("save", "#555555"));
    } else if (indices.size() == 1) {
        int id = indices.first().data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        
        // 2026-03-xx 按照用户要求：切换笔记时保持编辑模式，不再自动跳回预览
        m_editor->setNote(note, false);
        m_editor->togglePreview(false);
        m_metaPanel->setNote(note);
        m_editBtn->setEnabled(true);
        m_editBtn->setIcon(IconHelper::getIcon("save", "#2ecc71")); // 激活后显示绿色保存图标

    } else {
        m_metaPanel->setMultipleNotes(indices.size());
        m_editor->setPlainText(QString("已选中 %1 条笔记").arg(indices.size()));
        m_editBtn->setEnabled(false);
        m_editBtn->setIcon(IconHelper::getIcon("edit", "#555555"));
    }

}

void MainWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) {
        auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
        sc->setProperty("id", id);
    };

    add("mw_filter", [this](){ emit m_header->filterRequested(); });

#ifndef RAPID_MANAGER_TARGET
    // [PROFESSIONAL] 只有笔记端保留预览功能
    auto* previewSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_preview"), m_noteList, [this](){ doPreview(); }, Qt::WidgetShortcut);
    previewSc->setProperty("id", "mw_preview");
#endif

    add("mw_meta", [this](){ 
        bool current = m_metaPanel->isVisible();
        emit m_header->metadataToggled(!current); 
    });
    add("mw_refresh", [this](){ refreshData(); });
    add("mw_search", [this](){ m_header->focusSearch(); });
    add("mw_new", [this](){ doNewIdea(); });
    add("mw_favorite", [this](){ doToggleFavorite(); });
    add("mw_pin", [this](){ doTogglePin(); });
    add("mw_stay_on_top", [this](){
        if (m_header) {
            auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
            if (btn) btn->click();
        }
    });

#ifdef RAPID_MANAGER_TARGET
    // 2026-03-xx 按照用户最高要求：管理端不准弹出任何其他界面，锁定 Ctrl+S 为本地保存
    add("mw_edit", [this](){ m_editBtn->click(); });
#else
    add("mw_edit", [this](){ doEditSelected(); });
#endif
    add("mw_extract", [this](){ doExtractContent(); });
    add("mw_move_up", [this](){ doMoveNote(DatabaseManager::Up); });
    add("mw_move_down", [this](){ doMoveNote(DatabaseManager::Down); });
    add("mw_lock_cat", [this](){
        int catId = -1;
        // 1. 优先获取侧边栏当前选中的分类
        QModelIndex sidebarIdx = m_partitionTree->currentIndex();
        if (sidebarIdx.isValid() && sidebarIdx.data(CategoryModel::TypeRole).toString() == "category") {
            catId = sidebarIdx.data(CategoryModel::IdRole).toInt();
        }
        // 2. 若侧边栏未选中，则回退到当前视图对应的分类
        if (catId == -1 && m_currentFilterType == "category" && m_currentFilterValue != -1) {
            catId = m_currentFilterValue.toInt();
        }

        if (catId != -1) {
            DatabaseManager::instance().lockCategory(catId);
            // 锁定后若处于该分类视图，强制切出，防止界面残留
            if (m_currentFilterType == "category" && m_currentFilterValue == catId) {
                m_currentFilterType = "all";
                m_currentFilterValue = -1;
            }
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已立即锁定</b>");
        }
    });
    add("mw_lock_all_cats", [this](){
        // 2026-03-xx 按照用户要求：Ctrl+Shift+S 闪速锁定所有分类
        DatabaseManager::instance().lockAllCategories();
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 所有分类已闪速锁定</b>");
    });
    // [MODIFIED] 2026-03-xx 切换加锁分类显示/隐藏逻辑已迁移至 eventFilter 物理层，避免被快捷键系统抢占。
    // add("mw_toggle_locked_visibility", ...);

    // [PROFESSIONAL] 将删除快捷键绑定到列表，允许侧边栏通过 eventFilter 独立处理 Del 键
    auto* delSoftSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_soft"), m_noteList, [this](){ doDeleteSelected(false); }, Qt::WidgetShortcut);
    delSoftSc->setProperty("id", "mw_delete_soft");
    auto* delHardSc = new QShortcut(ShortcutManager::instance().getShortcut("mw_delete_hard"), m_noteList, [this](){ doDeleteSelected(true); }, Qt::WidgetShortcut);
    delHardSc->setProperty("id", "mw_delete_hard");

    add("mw_copy_tags", [this](){ doCopyTags(); });
    add("mw_paste_tags", [this](){ doPasteTags(); });
    add("mw_repeat_action", [this](){ doRepeatAction(); }); // [USER_REQUEST] 2026-03-14 F4重复上一次操作
    add("mw_show_all", [this](){
        m_currentFilterType = "all";
        m_currentFilterValue = -1;
        m_currentPage = 1;

        // 清除侧边栏选中状态
        m_systemTree->selectionModel()->clearSelection();
        m_systemTree->setCurrentIndex(QModelIndex());
        m_partitionTree->selectionModel()->clearSelection();
        m_partitionTree->setCurrentIndex(QModelIndex());

        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
    });
    add("mw_close", [this](){ close(); });

    for (int i = 0; i <= 5; ++i) {
        add(QString("mw_rating_%1").arg(i), [this, i](){ doSetRating(i); });
    }
}

void MainWindow::updateShortcuts() {
    // Note: m_shortcutActions was partially used in old version, but we should use QShortcut list
    // Let's fix the member variable usage to match NoteEditWindow/QuickWindow style
    auto shortcuts = findChildren<QShortcut*>();
    for (auto* sc : shortcuts) {
        QString id = sc->property("id").toString();
        if (!id.isEmpty()) {
            sc->setKey(ShortcutManager::instance().getShortcut(id));
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    QMainWindow::keyPressEvent(event);
}

void MainWindow::updateFocusLines() {
    QWidget* focus = QApplication::focusWidget();
    
    // 2026-03-13 修复逻辑：只有在侧边栏展开（可见且宽度大于10px）的情况下，才允许显示绿色焦点线
    // 宽度检查可以防止侧边栏在通过 Splitter 拖动折叠后的视觉残留。
    bool sidebarVisible = m_sidebarContainer && m_sidebarContainer->isVisible() && m_sidebarContainer->width() > 10;
    
    bool listFocus = (focus == m_noteList) && sidebarVisible;
    bool sidebarFocus = (focus == m_systemTree || focus == m_partitionTree) && sidebarVisible;

    if (m_listFocusLine) m_listFocusLine->setVisible(listFocus);
    if (m_sidebarFocusLine) m_sidebarFocusLine->setVisible(sidebarFocus);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // [CRITICAL] 抢占式拦截：在快捷键系统处理前捕获 Ctrl+S 和 Ctrl+Alt+S
    // 这能防止 QShortcut 或系统热键在 KeyPress 之前吞掉事件。
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        // [MODIFIED] 精确抢占：仅拦截 Ctrl+Alt+S 和 纯 Ctrl+S。
        // 显式排除 ShiftModifier，让 Ctrl+Shift+S 能够被原生的 QShortcut 系统处理。
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            if (keyEvent->modifiers() & Qt::AltModifier || !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                event->accept();
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // [DEBUG] 追踪按键流：打印按键、修饰键以及当前焦点所在的组件名
        qDebug() << "[TRACE-MW] KeyPress:" << QKeySequence(keyEvent->key()).toString() 
                 << "Mods:" << keyEvent->modifiers() 
                 << "FocusWidget:" << (watched ? watched->objectName() : "None");

        // [MODIFIED] 2026-03-xx 顶级物理拦截逻辑：修正 Ctrl+S 与 Ctrl+Alt+S 冲突
        // 显式区分锁定逻辑与显示/隐藏逻辑，确保优先级。
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            auto mods = keyEvent->modifiers();
            
            // 情况 A: Ctrl + Alt + S -> 切换加锁分类显示/隐藏
            if (mods & Qt::AltModifier) {
                qDebug() << "[MainWindow] 物理拦截捕获到 Ctrl+Alt+S, 切换显示/隐藏。";
                auto& db = DatabaseManager::instance();
                db.toggleLockedCategoriesVisibility();
                bool isHidden = db.isLockedCategoriesHidden();
                
                // 漂移保护：隐藏后若处于加锁分类，切回全部
                if (isHidden && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    m_currentFilterType = "all";
                    m_currentFilterValue = -1;
                }
                
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), isHidden ? 
                    "<b style='color: #e67e22;'>[OK] 已隐藏加锁分类并强制重锁</b>" : 
                    "<b style='color: #2ecc71;'>[OK] 已显示所有分类并强制重锁</b>");
                return true;
            }
            
            // 情况 B: 纯 Ctrl + S -> 立即锁定当前分类 (排除 Shift 以免干扰 Ctrl+Shift+S)
            if (!(mods & Qt::ShiftModifier)) {
                qDebug() << "[MainWindow] 物理拦截捕获到 Ctrl+S, 准备执行上锁。";
                int catId = -1;
                QModelIndex sidebarIdx = m_partitionTree->currentIndex();
                if (sidebarIdx.isValid() && sidebarIdx.data(CategoryModel::TypeRole).toString() == "category") {
                    catId = sidebarIdx.data(CategoryModel::IdRole).toInt();
                }
                if (catId == -1 && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    catId = m_currentFilterValue.toInt();
                }

                if (catId != -1) {
                    DatabaseManager::instance().lockCategory(catId);
                    if (m_currentFilterType == "category" && m_currentFilterValue == catId) {
                        m_currentFilterType = "all";
                        m_currentFilterValue = -1;
                    }
                    refreshData();
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已物理锁定</b>");
                    return true; 
                }
            }
        }
    }

    // [MODIFIED] 2026-03-xx 物理级拦截原生 ToolTip
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateFocusLines();
    }

    if (watched == m_noteList && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        auto modifiers = keyEvent->modifiers();
        int key = keyEvent->key();

        // [MODIFIED] 2026-03-11 强制拦截 Ctrl+C 优先级：锁定在 QListView 内部处理之前
        // 彻底根除系统默认逻辑自动抓取 DisplayRole (标题) 的行为。
        if (key == Qt::Key_C && (modifiers & Qt::ControlModifier)) {
            doExtractContent();
            return true; // 拦截，严禁传递给原生逻辑
        }

        // 【新增需求】波浪键/Backspace 快捷回到全部数据视图
        if (keyEvent->key() == Qt::Key_QuoteLeft || keyEvent->key() == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab
        if (keyEvent->key() == Qt::Key_Tab) {
            // [CRITICAL] 列表 -> 侧边栏焦点切换：跳转至当前激活分类或用户分类首项
            if (m_partitionTree->isVisible()) {
                m_partitionTree->setFocus();
                if (!m_partitionTree->currentIndex().isValid()) {
                    m_partitionTree->setCurrentIndex(m_partitionModel->index(0, 0));
                }
            } else if (m_systemTree->isVisible()) {
                m_systemTree->setFocus();
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_F2) {
            QModelIndex current = m_noteList->currentIndex();
            if (current.isValid()) {
                QString oldTitle = current.data(NoteModel::TitleRole).toString();
                int noteId = current.data(NoteModel::IdRole).toInt();
                TitleEditorDialog dlg(oldTitle, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString newTitle = dlg.getText();
                    if (!newTitle.isEmpty() && newTitle != oldTitle) {
                        DatabaseManager::instance().updateNoteState(noteId, "title", newTitle);
                        refreshData();
                    }
                }
            }
            return true;
        }
    }

    if ((watched == m_partitionTree || watched == m_systemTree) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

        // 【新增需求】波浪键/Backspace 快捷回到全部数据视图
        if (key == Qt::Key_QuoteLeft || key == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        if (key == Qt::Key_F2) {
            if (watched == m_partitionTree) {
                QModelIndex current = m_partitionTree->currentIndex();
                if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                    // [CRITICAL] 锁定：统一使用行内编辑模式，严禁改为弹出对话框，以保持与 QuickWindow 的逻辑一致性
                    m_partitionTree->edit(current);
                }
            }
            return true;
        }

        // [USER_REQUEST] 焦点切换快捷键从 Shift 改为 Tab
        if (key == Qt::Key_Tab && (watched == m_partitionTree || watched == m_systemTree)) {
            // [CRITICAL] 侧边栏 -> 列表焦点切换：自动选中首项或恢复当前选中项
            m_noteList->setFocus();
            auto* model = m_noteList->model();
            if (model && !m_noteList->currentIndex().isValid() && model->rowCount() > 0) {
                m_noteList->setCurrentIndex(model->index(0, 0));
            }
            return true;
        }

        if (key == Qt::Key_Delete) {
            if (watched == m_partitionTree) {
                auto selected = m_partitionTree->selectionModel()->selectedIndexes();
                if (!selected.isEmpty()) {
                    QString confirmMsg = selected.size() > 1 ? QString("确定要删除选中的 %1 个分类及其下所有内容吗？").arg(selected.size()) : "确定要删除选中的分类及其下所有内容吗？";
                    FramelessMessageBox dlg("确认删除", confirmMsg, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        QList<int> ids;
                        for (const auto& idx : selected) {
                            if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                                ids << idx.data(CategoryModel::IdRole).toInt();
                            }
                        }
                        DatabaseManager::instance().softDeleteCategories(ids);
                        refreshData();
                    }
                }
            } else if (watched == m_systemTree) {
                QModelIndex index = m_systemTree->currentIndex();
                if (index.isValid()) {
                    QString type = index.data(CategoryModel::TypeRole).toString();
                    if (type == "trash") {
                        FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
                        if (dlg.exec() == QDialog::Accepted) {
                            DatabaseManager::instance().emptyTrash();
                            refreshData();
                        }
                    }
                }
            }
            return true;
        }

        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            QModelIndex current = m_partitionTree->currentIndex();
            if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = current.data(CategoryModel::IdRole).toInt();
                DatabaseManager::MoveDirection dir;
                
                if (key == Qt::Key_Up) {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Top : DatabaseManager::Up;
                } else {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Bottom : DatabaseManager::Down;
                }

                if (DatabaseManager::instance().moveCategory(catId, dir)) {
                    refreshData();
                    // 重新选中该分类 (refreshData 会刷新整个模型)
                    // 注意：refreshData 内部有恢复选中的逻辑，但它是基于 NameRole 的。
                    // 既然 sort_order 变了，我们需要确保它还在选中状态。
                    return true;
                }
            }
        }
        
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // [NEW] 单行输入框箭头导航逻辑：↑ 移至首部，↓ 移至尾部
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            // [CRITICAL] 顶级两段式逻辑：顶栏搜索框按下 Esc 时返回界面
            if (m_header && watched == m_header->searchEdit()) {
                if (!m_header->searchEdit()->text().isEmpty()) {
                    m_header->searchEdit()->clear();
                } else {
                    m_noteList->setFocus();
                }
                return true;
            }
            
            // 顶栏页码框按下 Esc 时返回界面
            if (m_header && watched == m_header->pageInput()) {
                m_noteList->setFocus();
                return true;
            }

            // [CRITICAL] 如果焦点在元数据面板的输入框中
            if (m_metaPanel && watched == m_metaPanel->m_tagEdit) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit && !edit->text().isEmpty()) {
                    edit->clear();
                } else {
                    m_noteList->setFocus();
                }
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onTagSelected(const QModelIndex& index) {
    m_currentFilterType = index.data(CategoryModel::TypeRole).toString();
    if (m_currentFilterType == "category") {
        m_currentFilterValue = index.data(CategoryModel::IdRole).toInt();
        StringUtils::recordRecentCategory(m_currentFilterValue.toInt());
        DatabaseManager::instance().setActiveCategoryId(m_currentFilterValue.toInt());
    } else {
        m_currentFilterValue = -1;
        DatabaseManager::instance().setActiveCategoryId(-1);
    }
    m_currentPage = 1;
    refreshData();
}

void MainWindow::showContextMenu(const QPoint& pos) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_noteList->indexAt(pos);
        if (index.isValid()) {
            m_noteList->setCurrentIndex(index);
            selected << index;
        }
    }

    int selCount = selected.size();
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }"); // 2026-03-13 修改悬停色为灰色，防止与蓝色图标视觉重合

    auto getHint = [](const QString& id) {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        return seq.isEmpty() ? "" : " (" + seq.toString(QKeySequence::NativeText).replace("+", " + ") + ")";
    };

    // [USER_REQUEST] 列表空白处右键弹出“新建数据”
    if (selCount == 0) {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), " 新建数据" + getHint("mw_new"), this, &MainWindow::doNewIdea);
        menu.exec(m_noteList->mapToGlobal(pos));
        return;
    }

    if (selCount == 1) {
#ifdef RAPID_MANAGER_TARGET
        menu.addAction(IconHelper::getIcon("edit", "#4FACFE", 18), "编辑内容 (F2)", [this](){
            m_editor->setFocus();
        });
#else
        // 2026-03-13 按照用户要求：eye 图标颜色统一为 #41F2F2
        menu.addAction(IconHelper::getIcon("eye", "#41F2F2", 18), "预览" + getHint("mw_preview"), this, &MainWindow::doPreview);
#endif
        
        QString content = selected.first().data(NoteModel::ContentRole).toString();
        QString type = selected.first().data(NoteModel::TypeRole).toString();
        
        if (type == "image") {
            menu.addAction(IconHelper::getIcon("screenshot_ocr", "#3498db", 18), "从图提取文字", this, &MainWindow::doOCR);
        }

        // 智能检测网址并显示打开菜单
        QString firstUrl = StringUtils::extractFirstUrl(content);
        if (!firstUrl.isEmpty()) {
            menu.addAction(IconHelper::getIcon("link", "#17B345", 18), "打开链接", [firstUrl]() {
                QDesktopServices::openUrl(QUrl(firstUrl));
            });
        }

        // [CRITICAL] 锁定：智能路径检测逻辑。支持托管项目（attachments/）及磁盘绝对路径的智能识别。
        // 即使类型为 text，若内容指向有效物理路径，也必须显示“在资源管理器中显示”菜单。严禁移除。
        bool isPath = (type == "file" || type == "local_file" || type == "local_folder" || type == "local_batch");
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        QString path = content;

        if (!isPath) {
            // [USER_REQUEST] 智能路径检测：即使类型不是文件，如果内容本身是一个有效的绝对路径，也支持定位
            if (QFileInfo(plainContent).exists() && QFileInfo(plainContent).isAbsolute()) {
                isPath = true;
                path = plainContent;
            }
        }

        if (isPath) {
            if (path.startsWith("attachments/")) {
                path = QCoreApplication::applicationDirPath() + "/" + path;
            }

            // [UX] 增加路径有效性检查：如果物理文件已丢失，菜单显示为置灰的“无效项目”
            if (QFileInfo::exists(path)) {
                menu.addAction(IconHelper::getIcon("folder", "#3A90FF", 18), "在资源管理器中显示", [path]() {
                    StringUtils::locateInExplorer(path, true);
                });
            } else {
                QAction* invalidAction = menu.addAction(IconHelper::getIcon("folder", "#555555", 18), "无效项目");
                invalidAction->setEnabled(false);
                invalidAction->setProperty("tooltipText", "该数据对应的原始文件已在磁盘中丢失或被移动");
            }
        }
        
#ifndef RAPID_MANAGER_TARGET
        menu.addAction(IconHelper::getIcon("calendar", "#4facfe", 18), "生成待办事项", [this, selected]() {
            int noteId = selected.first().data(NoteModel::IdRole).toInt();
            QString title = selected.first().data(NoteModel::TitleRole).toString();
            QString content = selected.first().data(NoteModel::ContentRole).toString();
            
            DatabaseManager::Todo t;
            t.title = "待办: " + title;
            t.content = StringUtils::htmlToPlainText(content);
            t.noteId = noteId;
            t.startTime = QDateTime::currentDateTime();
            t.endTime = t.startTime.addSecs(3600);
            
            DatabaseManager::instance().addTodo(t);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已成功转化为待办事项</b>");
        });
#endif
    }
    
    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制 (%1)").arg(selCount), this, &MainWindow::doExtractContent);
    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑" + getHint("mw_edit"), this, &MainWindow::doEditSelected);

        // [USER_REQUEST] 2026-03-14 右键菜单新增：复制/粘贴标签
        QString tags = selected.first().data(NoteModel::TagsRole).toString();
        if (!tags.trimmed().isEmpty()) {
            menu.addAction(IconHelper::getIcon("copy_tags", "#9b59b6", 18), "复制标签" + getHint("mw_copy_tags"), [this](){
                auto selected = m_noteList->selectionModel()->selectedIndexes();
                if (selected.isEmpty()) return;
                QString tags = selected.first().data(NoteModel::TagsRole).toString();
                if (!tags.isEmpty()) {
                    DatabaseManager::setTagClipboard(tags.split(",", Qt::SkipEmptyParts));
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已复制标签</b>");
                }
            });
        }
        
        // [USER_REQUEST] 傻逼逻辑修复：仅当标签剪贴板不为空时，才显示“粘贴标签”选项
        if (!DatabaseManager::getTagClipboard().isEmpty()) {
            menu.addAction(IconHelper::getIcon("paste_tags", "#e67e22", 18), "粘贴标签" + getHint("mw_paste_tags"), this, &MainWindow::doPasteTags);
        }

        menu.addSeparator();
    }

    auto* ratingMenu = menu.addMenu(IconHelper::getIcon("star", "#f39c12", 18), QString("标记星级 (%1)").arg(selCount));
    ratingMenu->setStyleSheet(menu.styleSheet());
    auto* starGroup = new QActionGroup(this);
    int currentRating = (selCount == 1) ? selected.first().data(NoteModel::RatingRole).toInt() : -1;
    
    for (int i = 1; i <= 5; ++i) {
        QString stars = QString("★").repeated(i);
        QAction* action = ratingMenu->addAction(stars, [this, i]() { doSetRating(i); });
        action->setCheckable(true);
        if (i == currentRating) action->setChecked(true);
        starGroup->addAction(action);
    }
    ratingMenu->addSeparator();
    ratingMenu->addAction("清除评级", [this]() { doSetRating(0); });

    bool isFavorite = (selCount == 1) && selected.first().data(NoteModel::FavoriteRole).toBool();
    // 2026-03-13 按照用户要求：收藏图标统一使用 bookmark_filled，颜色统一为 #F2B705
    menu.addAction(IconHelper::getIcon("bookmark_filled", "#F2B705", 18), 
                   isFavorite ? "取消收藏" : "添加收藏" + getHint("mw_favorite"), this, &MainWindow::doToggleFavorite);

    bool isPinned = (selCount == 1) && selected.first().data(NoteModel::PinnedRole).toBool();
    // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色 (#FF551C)
    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18), 
                   isPinned ? "取消置顶" : "置顶选中项" + getHint("mw_pin"), this, &MainWindow::doTogglePin);
    
    menu.addSeparator();

    auto* catMenu = menu.addMenu(IconHelper::getIcon("branch", "#cccccc", 18), QString("移动选中项到分类 (%1)").arg(selCount));
    catMenu->setStyleSheet(menu.styleSheet());
    catMenu->addAction(IconHelper::getIcon("uncategorized", "#e67e22", 18), "未分类", [this]() { doMoveToCategory(-1); });
    
    QVariantList recentCats = StringUtils::getRecentCategories();
    auto allCategories = DatabaseManager::instance().getAllCategories();
    QMap<int, QVariantMap> catMap;
    for (const auto& cat : std::as_const(allCategories)) catMap[cat.value("id").toInt()] = cat;

    int count = 0;
    for (const auto& v : std::as_const(recentCats)) {
        if (count >= 10) break;
        int cid = v.toInt();
        if (catMap.contains(cid)) {
            const auto& cat = catMap.value(cid);
            catMenu->addAction(IconHelper::getIcon("branch", cat.value("color").toString(), 18), cat.value("name").toString(), [this, cid]() {
                doMoveToCategory(cid);
            });
            count++;
        }
    }

    menu.addSeparator();
    if (m_currentFilterType == "trash") {
        // [MODIFIED] 2026-03-xx 按照用户要求：更精细化恢复文案，区分“单选恢复”与“多选批量恢复”
        QString restoreText = selected.size() > 1 ? QString("恢复选中项 (%1)").arg(selected.size()) : "恢复";
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), restoreText, [this, selected](){
            QList<int> noteIds;
            QList<int> catIds;
            for (const auto& index : selected) {
                QString type = index.data(NoteModel::TypeRole).toString();
                int id = index.data(NoteModel::IdRole).toInt();
                if (type == "deleted_category") catIds << id;
                else noteIds << id;
            }
            // 批量恢复笔记（不再强制设为 NULL，保留原分类关系）
            if (!noteIds.isEmpty()) DatabaseManager::instance().updateNoteStateBatch(noteIds, "is_deleted", 0);
            // 批量恢复分类及其层级
            if (!catIds.isEmpty()) DatabaseManager::instance().restoreCategories(catIds);
            refreshData();
            QString successMsg = selected.size() > 1 ? QString("[OK] 已恢复选中的 %1 个项目").arg(selected.size()) : "[OK] 已恢复 1 个项目";
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>" + successMsg + "</b>");
        });

        menu.addAction(IconHelper::getIcon("refresh", "#3498db", 18), "全部恢复 (还原所有)", [this](){
            if (DatabaseManager::instance().restoreAllFromTrash()) {
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已将回收站内容全量还原</b>");
            }
        });

        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "彻底删除 (不可逆)", [this](){ doDeleteSelected(true); });
    } else {
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "移至回收站" + getHint("mw_delete_soft"), [this](){ doDeleteSelected(false); });
    }

    if (m_currentFilterType != "recently_visited") {
        menu.addSeparator();
        auto* sortMenu = menu.addMenu(IconHelper::getIcon("list_ol", "#aaaaaa", 18), "排列");
        sortMenu->setStyleSheet(menu.styleSheet());
        
        sortMenu->addAction("上移" + getHint("mw_move_up"), [this](){ doMoveNote(DatabaseManager::Up); });
        sortMenu->addAction("下移" + getHint("mw_move_down"), [this](){ doMoveNote(DatabaseManager::Down); });
        sortMenu->addAction("移至顶部", [this](){ doMoveNote(DatabaseManager::Top); });
        sortMenu->addAction("移至底部", [this](){ doMoveNote(DatabaseManager::Bottom); });
        sortMenu->addSeparator();
        sortMenu->addAction("按标题 A→Z 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, true, m_filterPanel->getCheckedCriteria());
        });
        sortMenu->addAction("按标题 Z→A 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, false, m_filterPanel->getCheckedCriteria());
        });
    }

    menu.exec(QCursor::pos());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveLayout() {
    // 2026-03-xx 按照用户要求：使用默认 QSettings 以支持 RapidManager 独立配置域
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    if (m_header) {
        auto* btn = m_header->findChild<QPushButton*>("btnStayOnTop");
        if (btn) {
            settings.setValue("stayOnTop", btn->isChecked());
        }
    }
    
    QSplitter* splitter = findChild<QSplitter*>();
    if (splitter) {
        settings.setValue("splitterState", splitter->saveState());
    }

    // 保存面板可见性
    settings.setValue("showFilter", m_filterWrapper->isVisible());
    settings.setValue("showMetadata", m_metaPanel->isVisible());
    settings.endGroup();
}

void MainWindow::restoreLayout() {
    // 2026-03-xx 按照用户要求：使用默认 QSettings 以支持 RapidManager 独立配置域
    QSettings settings;
    settings.beginGroup("MainWindow");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("windowState")) {
        restoreState(settings.value("windowState").toByteArray());
    }
    
    QSplitter* splitter = findChild<QSplitter*>();
    if (splitter && settings.contains("splitterState")) {
        splitter->restoreState(settings.value("splitterState").toByteArray());
    }

    // 恢复面板可见性
    bool showFilter = settings.value("showFilter", true).toBool();
    bool showMetadata = settings.value("showMetadata", true).toBool();
    
    m_filterWrapper->setVisible(showFilter);
    m_header->setFilterActive(showFilter);
    
    m_metaPanel->setVisible(showMetadata);
    m_header->setMetadataActive(showMetadata);

    bool stayOnTop = settings.value("stayOnTop", false).toBool();
    settings.endGroup();

    auto* btnStay = m_header->findChild<QPushButton*>("btnStayOnTop");
    if (btnStay) {
        btnStay->setChecked(stayOnTop);
        // 手动应用图标 (HeaderBar 不会自动切换图标，除非触发 toggled 信号)
        // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后图标变为橙色。
        btnStay->setIcon(IconHelper::getIcon(stayOnTop ? "pin_vertical" : "pin_tilted", stayOnTop ? "#FF551C" : "#aaaaaa", 20));
        
        if (stayOnTop) {
            #ifdef Q_OS_WIN
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            #else
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
            #endif
        }
    }

}


void MainWindow::doPreview() {
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 独立版已移除预览窗口</b>");
}
void MainWindow::updatePreviewContent() {}

void MainWindow::doDeleteSelected(bool physical) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    bool inTrash = (m_currentFilterType == "trash");
    
    if (physical || inTrash) {
        QString title = inTrash ? "清空项目" : "彻底删除";
        QString text = QString("确定要永久删除选中的 %1 条数据吗？\n此操作不可逆，数据将无法找回。").arg(selected.count());
        
        FramelessMessageBox msg(title, text, this);
        QList<int> idsToDelete;
        for (const auto& index : std::as_const(selected)) idsToDelete << index.data(NoteModel::IdRole).toInt();
        
        if (msg.exec() == QDialog::Accepted) {
            if (!idsToDelete.isEmpty()) {
                DatabaseManager::instance().deleteNotesBatch(idsToDelete);
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已永久删除 %1 条数据</b>").arg(idsToDelete.size()));
            }
        }
    } else {
        QList<int> ids;
        for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().softDeleteNotes(ids);
        refreshData();
    }
}

void MainWindow::doToggleFavorite() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_favorite");
    }
    refreshData();
}

void MainWindow::doTogglePin() {
    QWidget* focus = QApplication::focusWidget();
    
    // [USER_REQUEST] 统一快捷键 Alt+D: 焦点在侧边栏则置顶分类，焦点在列表则置顶数据
    if (focus == m_systemTree || focus == m_partitionTree) {
        QModelIndex index = (focus == m_systemTree) ? m_systemTree->currentIndex() : m_partitionTree->currentIndex();
        if (index.isValid()) {
            // [CRITICAL] 处理 ProxyModel 映射，确保在搜索过滤状态下依然能准确获取分类 ID
            QModelIndex srcIdx = index;
            // MainWindow 目前分类树暂未使用 ProxyModel，但为防御性编程，我们检查其 model 类型。
            // 经查，MainWindow 的 m_systemModel 和 m_partitionModel 是直接的 CategoryModel。
            
            if (index.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = index.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().toggleCategoryPinned(catId);
                refreshData();
                return;
            }
        }
    }

    // 默认执行列表项置顶逻辑
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().toggleNoteState(id, "is_pinned");
    }
    refreshData();
}

void MainWindow::doNewIdea() {
    // 2026-03-xx 按照用户要求：RapidManager 移除外部新建窗口，直接在库中插入空笔记并刷新焦点
    int catId = getCurrentCategoryId();
    int newId = DatabaseManager::instance().addNote("新记录", "", {}, "", catId);
    if (newId > 0) {
        refreshData();
        // 在模型中查找并选中新项
        for (int i = 0; i < m_noteModel->rowCount(); ++i) {
            QModelIndex idx = m_noteModel->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == newId) {
                m_noteList->setCurrentIndex(idx);
                m_editor->setFocus();
                break;
            }
        }
    }
}

void MainWindow::doCreateByLine(bool fromClipboard) {
    QString text;
    if (fromClipboard) {
        text = QApplication::clipboard()->text();
    } else {
        auto selected = m_noteList->selectionModel()->selectedIndexes();
        QStringList contents;
        for (const auto& index : selected) {
            contents << StringUtils::htmlToPlainText(index.data(NoteModel::ContentRole).toString());
        }
        text = contents.join("\n");
    }

    if (text.trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 没有有效内容可供拆分</b>");
        return;
    }

    QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    int catId = getCurrentCategoryId();
    
    DatabaseManager::instance().beginBatch();
    int count = 0;
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        
        QString title, content;
        StringUtils::smartSplitLanguage(trimmed, title, content);
        DatabaseManager::instance().addNote(title, content, {}, "", catId);
        count++;
    }
    DatabaseManager::instance().endBatch();
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已成功按行创建 %1 条数据</b>").arg(count));
}

void MainWindow::doOCR() {
    // 2026-03-xx 极致剥离：移除文字识别后台服务支持
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 独立版已移除 OCR 文字识别功能</b>");
}

void MainWindow::doExtractContent() {
    // [MODIFIED] 2026-03-11 按照用户要求，重构复制逻辑：复制内容优先策略，排除标题，支持多类型，不显示提示反馈。
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QList<QVariantMap> notes;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        // [CRITICAL] 锁定：内容提取视为实际操作，必须显式记录访问。严禁移除。
        DatabaseManager::instance().recordAccess(id); 
        notes << DatabaseManager::instance().getNoteById(id);
    }

    StringUtils::copyNotesToClipboard(notes);
}

void MainWindow::doEditSelected() {
    // 2026-03-xx 按照用户要求：RapidManager 移除外部编辑窗口，调用内部保存
    m_editBtn->click();
}

void MainWindow::doSetRating(int rating) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "rating", rating);
    }
    refreshData();
}

void MainWindow::doMoveToCategory(int catId) {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    
    DatabaseManager::instance().moveNotesToCategory(ids, catId);
    // [USER_REQUEST] 2026-03-14 记录动作，用于 F4 重复操作
    ActionRecorder::instance().recordMoveToCategory(catId);
    
    if (catId != -1) {
        StringUtils::recordRecentCategory(catId);
    }
    refreshData();
}

void MainWindow::doMoveNote(DatabaseManager::MoveDirection dir) {
    QModelIndex index = m_noteList->currentIndex();
    if (!index.isValid()) return;
    
    int id = index.data(NoteModel::IdRole).toInt();
    if (DatabaseManager::instance().moveNote(id, dir, m_currentFilterType, m_currentFilterValue, m_filterPanel->getCheckedCriteria())) {
        // 刷新后由于 ID 相同，refreshData 会自动恢复选中项
    }
}


void MainWindow::doCopyTags() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    // 获取选中的第一个项的标签
    int id = selected.first().data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    QString tagsStr = note.value("tags").toString();
    QStringList tags = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : tags) t = t.trimmed();

    DatabaseManager::setTagClipboard(tags);
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已复制 %1 个标签</b>").arg(tags.size()), 700);
}

void MainWindow::doPasteTags() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tagsToPaste = DatabaseManager::getTagClipboard();
    if (tagsToPaste.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[!] 标签剪贴板为空</b>");
        return;
    }

    // 直接覆盖标签 (符合粘贴语义)
    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    DatabaseManager::instance().updateNoteStateBatch(ids, "tags", tagsToPaste.join(", "));

    // 刷新数据以显示新标签
    refreshData();
    // [USER_REQUEST] 2026-03-14 记录动作，用于 F4 重复操作
    ActionRecorder::instance().recordPasteTags(tagsToPaste);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已覆盖粘贴标签至 %1 条数据</b>").arg(selected.size()));
}

void MainWindow::doRepeatAction() {
    auto selected = m_noteList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f1c40f;'>[提示] 请先选中一条笔记</b>");
        return;
    }

    auto actionType = ActionRecorder::instance().getLastActionType();
    
    if (actionType == ActionRecorder::ActionType::None) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[提示] 目前没有可重复的操作记录</b>");
        return;
    }

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();

    if (actionType == ActionRecorder::ActionType::PasteTags) {
        QStringList tagsList = ActionRecorder::instance().getLastActionData().toStringList();
        if (tagsList.isEmpty()) return;

        QString tagsStr = tagsList.join(", ");
        DatabaseManager::instance().updateNoteStateBatch(ids, "tags", tagsStr);
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已重复：%1 条数据粘贴标签</b>").arg(ids.size()));
    } 
    else if (actionType == ActionRecorder::ActionType::MoveToCategory) {
        int catId = ActionRecorder::instance().getLastActionData().toInt();
        DatabaseManager::instance().moveNotesToCategory(ids, catId);
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已重复：%1 条分类归位</b>").arg(ids.size()));
    }
}

void MainWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 导入完成，共处理 %1 个项目</b>").arg(totalCount));
}

void MainWindow::doImportFolder(int catId) {
    // 2026-03-xx 按照用户最高要求修复傻逼逻辑：升级原生单选为多选文件夹导入，彻底提升效率
    QFileDialog dialog(this);
    dialog.setWindowTitle("选择导入文件夹 (可多选)");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true); // 强制使用非原生对话框以支持多选

    // 允许在文件视图中多选
    QListView *listView = dialog.findChild<QListView*>("listView");
    if (listView) listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QTreeView *treeView = dialog.findChild<QTreeView*>("treeView");
    if (treeView) treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (dialog.exec() != QDialog::Accepted) return;
    QStringList dirs = dialog.selectedFiles();
    if (dirs.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(dirs, catId);
    
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 批量导入完成，共处理 %1 个目录共 %2 个项目</b>").arg(dirs.size()).arg(totalCount));
}

static bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        if (!QDir().mkpath(dstPath)) return false;
        QDir srcDir(srcPath);
        QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (!copyRecursively(srcPath + "/" + entry, dstPath + "/" + entry)) return false;
        }
        return true;
    } else {
        return QFile::copy(srcPath, dstPath);
    }
}

void MainWindow::doExportCategory(int catId, const QString& catName) {
    if (!verifyExportPermission()) return;
    FileStorageHelper::exportCategory(catId, catName, this);
}

void MainWindow::updateToolboxStatus(bool active) {
    // 2026-03-22 [NEW] 同步工具箱按钮颜色状态到 HeaderBar
    if (m_header) {
        m_header->updateToolboxStatus(active);
    }
}

bool MainWindow::verifyExportPermission() {
    // 2026-03-20 按照用户要求，所有导出操作前必须进行身份验证
    QSettings settings;
#ifdef RAPID_MANAGER_TARGET
    settings.beginGroup("RapidManager");
#else
    settings.beginGroup("QuickWindow");
#endif
    QString realPwd = settings.value("appPassword").toString();
    settings.endGroup();

    // 1. 如果用户未设置密码，根据方案，引导用户先进行安全设置
    if (realPwd.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e67e22;'>[安全拦截] 请先在【系统设置】中设置“应用锁定密码”后再执行导出</b>", 3000);
        return false;
    }

    // 2. 弹出统一的验证对话框
    PasswordVerifyDialog dlg("导出身份验证", "当前操作涉及数据导出，请输入应用锁定密码以继续：", this);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    // 3. 校验密码
    if (dlg.password() != realPwd) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>❌ 密码验证失败，导出已终止</b>", 2000);
        return false;
    }

    // 验证通过
    return true;
}
