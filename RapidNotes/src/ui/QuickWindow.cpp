#include "ToolTipOverlay.h"
#include "QuickWindow.h"
#include "RecentNotesWindow.h"
#include <QDebug>

#include "SearchLineEdit.h"
#include "CleanListView.h"
#include "ClickableLineEdit.h"
#include "CategoryLockWidget.h"
#include "DropTreeView.h"
#include "QuickPreview.h"
#include "PasswordVerifyDialog.h"
#include "FilterPanel.h"
#include "../models/NoteModel.h"
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QListView>
#include <QPushButton>
#include "../models/CategoryModel.h"
#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "TitleEditorDialog.h"
#include "../core/FileStorageHelper.h"
#include "AdvancedTagSelector.h"
#include "IconHelper.h"
#include "QuickNoteDelegate.h"
#include "CategoryDelegate.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <utility>
#include <QScreen>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QMenu>
#include <QWindow>
#include <QShortcut>
#include <QKeySequence>
#include "../core/KeyboardHook.h"
#include <QClipboard>
#include <QMimeData>
#include <QDrag>
#include <QTimer>
#include <QApplication>
#include <QElapsedTimer>
#include <QActionGroup>
#include <QAction>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QUrl>
#include <QBuffer>
#include <QScrollBar>
#include <QWheelEvent>
#include <QRegularExpression>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
#include "FramelessDialog.h"
#include "../core/ActionRecorder.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include "PasswordVerifyDialog.h"
#include "CategoryPasswordDialog.h"
#include "SettingsWindow.h"
#include "../core/ShortcutManager.h"
#include "../core/HotkeyManager.h"
#include <QRandomGenerator>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFont>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTransform>
#include <QtMath>
#include <QThreadPool>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

class AppLockWidget : public QWidget {
    Q_OBJECT
public:
    AppLockWidget(const QString& correctPassword, QWidget* parent = nullptr)
        : QWidget(parent), m_correctPassword(correctPassword) {
        setObjectName("AppLockWidget");
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_StyledBackground);

        auto* layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(20);

        
        setStyleSheet("QWidget#AppLockWidget { background-color: #1C1C1C; border-radius: 10px; } "
                      "QLabel { background: transparent; border: none; }");

        auto* appTitle = new QLabel("懒人笔记");
        appTitle->setStyleSheet("color: white; font-size: 24px; font-weight: bold; margin-bottom: 20px;");
        appTitle->setAlignment(Qt::AlignCenter);
        layout->addWidget(appTitle);

        
        auto* lockIcon = new QLabel();

        lockIcon->setPixmap(IconHelper::getIcon("lock_secure", "#00A650").pixmap(64, 64));
        lockIcon->setAlignment(Qt::AlignCenter);
        layout->addWidget(lockIcon);

        
        auto* titleLabel = new QLabel("已锁定");

        titleLabel->setStyleSheet("color: #00A650; font-size: 18px; font-weight: bold;");
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);

        
        QSettings settings("RapidNotes", "QuickWindow");
        QString hint = settings.value("appPasswordHint", "请输入启动密码").toString();
        auto* hintLabel = new QLabel(hint);
        hintLabel->setStyleSheet("color: #666666; font-size: 12px;");
        hintLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(hintLabel);

        
        m_pwdEdit = new QLineEdit();
        m_pwdEdit->setEchoMode(QLineEdit::Password);
        m_pwdEdit->setPlaceholderText("请输入密码");
        m_pwdEdit->setFixedWidth(240);
        m_pwdEdit->setFixedHeight(36);
        m_pwdEdit->setAlignment(Qt::AlignCenter);
        m_pwdEdit->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2A2A2A; border: 1px solid #333; border-radius: 6px;"
            "  color: white; font-size: 14px;"
            "}"
            "QLineEdit:focus { border: 1px solid #3A90FF; }"
        );
        connect(m_pwdEdit, &QLineEdit::returnPressed, this, &AppLockWidget::handleVerify);
        layout->addWidget(m_pwdEdit, 0, Qt::AlignHCenter);

        
        m_closeBtn = new QPushButton(this);
        m_closeBtn->setIcon(IconHelper::getIcon("close", "#aaaaaa"));
        m_closeBtn->setIconSize(QSize(18, 18));
        m_closeBtn->setFixedSize(32, 32);
        m_closeBtn->setCursor(Qt::PointingHandCursor);
        m_closeBtn->setStyleSheet(
            "QPushButton { border: none; border-radius: 4px; background: transparent; } "
            "QPushButton:hover { background-color: #E81123; }"
        );
        connect(m_closeBtn, &QPushButton::clicked, []() { QApplication::quit(); });

        
        m_pwdEdit->setFocus();
    }

    void focusInput() {
        m_pwdEdit->setFocus();
        m_pwdEdit->selectAll();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override {
        m_closeBtn->move(width() - m_closeBtn->width() - 10, 10);
        QWidget::resizeEvent(event);
    }

private slots:
    void handleVerify() {
        if (m_pwdEdit->text() == m_correctPassword) {
            startFadeOut();
        } else {
            startShake();
        }
    }

    void startFadeOut() {
        auto* opacityEffect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(opacityEffect);
        auto* animation = new QPropertyAnimation(opacityEffect, "opacity");
        animation->setDuration(300);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            emit unlocked();
            this->deleteLater();
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void startShake() {
        m_pwdEdit->clear();
        auto* anim = new QPropertyAnimation(m_pwdEdit, "pos");
        anim->setDuration(400);
        anim->setLoopCount(1);

        QPoint pos = m_pwdEdit->pos();
        anim->setKeyValueAt(0, pos);
        anim->setKeyValueAt(0.1, pos + QPoint(-10, 0));
        anim->setKeyValueAt(0.3, pos + QPoint(10, 0));
        anim->setKeyValueAt(0.5, pos + QPoint(-10, 0));
        anim->setKeyValueAt(0.7, pos + QPoint(10, 0));
        anim->setKeyValueAt(0.9, pos + QPoint(-10, 0));
        anim->setKeyValueAt(1, pos);

        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

signals:
    void unlocked();

private:
    QLineEdit* m_pwdEdit;
    QPushButton* m_closeBtn;
    QString m_correctPassword;
};

#define RESIZE_MARGIN 10

QuickWindow::QuickWindow(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint)
{

     setWindowTitle("懒人笔记");
    setAcceptDrops(true);
    
    // [UI-AESTHETIC] 2026-04-18 按照业务规范：DWM 圆角属性移动至 showEvent 延迟执行，防止启动时句柄不稳导致的崩溃
    
    // 初始化 UI 组件
setAttribute(Qt::WA_TranslucentBackground);
    
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setAttribute(Qt::WA_DeleteOnClose, false);

    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);

    initUI();

    // [STABILITY] 原生样式延迟到 showEvent 处理

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);

    QSettings settings("RapidNotes", "QuickWindow");
    m_isSidebarPersistent = settings.value("sidebarPersistent", true).toBool();
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, [this](){
        if (this->isVisible()) {
            refreshData();

            
            // refreshSidebar();
        }
    });

    connect(&DatabaseManager::instance(), &DatabaseManager::noteAdded, this, &QuickWindow::onNoteAdded);
    connect(&DatabaseManager::instance(), &DatabaseManager::noteUpdated, this, &QuickWindow::scheduleRefresh);
    connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, this, &QuickWindow::scheduleRefresh);
    connect(&DatabaseManager::instance(), &DatabaseManager::autoCategorizeEnabledChanged, this, &QuickWindow::updateAutoCategorizeButton);
    connect(&DatabaseManager::instance(), &DatabaseManager::appLockSettingsChanged, this, &QuickWindow::updateAppLockStatus);

    connect(&DatabaseManager::instance(), &DatabaseManager::activeCategoryIdChanged, this, [this](int id){
        // 2026-05-16 按照用户要求：彻底解耦。
        // 设定自动归档目标分类是一个后台参数变更行为，不应强制切换用户当前的浏览视图（如“最近访问”）。
        // 仅刷新 UI 状态：顶部按钮与侧边栏图标。
        updateAutoCategorizeButton(); 
        refreshSidebar();
    });

    connect(&DatabaseManager::instance(), &DatabaseManager::categoriesChanged, this, [this](){
        m_model->updateCategoryMap();

        
        if (m_currentFilterType == "category" && m_currentFilterValue != -1) {
            auto categories = DatabaseManager::instance().getAllCategories();
            for (const auto& cat : std::as_const(categories)) {
                if (cat.value("id").toInt() == m_currentFilterValue) {
                    m_currentCategoryColor = cat.value("color").toString();
                    if (m_currentCategoryColor.isEmpty()) m_currentCategoryColor = "#4a90e2";
                    applyListTheme(m_currentCategoryColor);
                    break;
                }
            }
        }

        
        updateAutoCategorizeButton();

        scheduleRefresh();
    });

    
    installEventFilter(this);
    
    // 2026-05-10 按照用户要求：同步启动时的侧边栏选中状态与底部标签
    for (int i = 0; i < m_systemProxyModel->rowCount(); ++i) {
        QModelIndex idx = m_systemProxyModel->index(i, 0);
        if (idx.data(CategoryModel::TypeRole).toString() == m_currentFilterType) {
            m_systemTree->setCurrentIndex(idx);
            updatePartitionStatus(idx.data(CategoryModel::NameRole).toString());
            break;
        }
    }

    // 确保主窗口初始化完毕后，立即加载默认视图的首屏数据
    scheduleRefresh();
}

void QuickWindow::initUI() {
    m_mainLayout = new QVBoxLayout(this);
    
    m_mainLayout->setContentsMargins(12, 12, 12, 12);

    auto* container = new QWidget();
    container->setObjectName("container");
    container->setMouseTracking(true); 
    container->setStyleSheet(
        "QWidget#container { background: #1E1E1E; border-radius: 10px; border: 1px solid #333; }"
        "QListView, QTreeView { background: transparent; border: none; color: #BBB; outline: none; }"
        "QTreeView::item { height: 22px; padding: 0px 4px; border-radius: 4px; }"
        "QTreeView::item:hover { background-color: #2a2d2e; }"
        "QTreeView::item:selected { background-color: transparent; color: white; }"
        "QListView::item { padding: 6px; border-bottom: 1px solid #2A2A2A; }"
    );

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(7);
    shadow->setColor(QColor(0, 0, 0, 90));   
    shadow->setOffset(0, 2);                 
    container->setGraphicsEffect(shadow);

    auto* containerLayout = new QHBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    
    auto* leftContent = new QWidget();
    leftContent->setObjectName("leftContent");
    leftContent->setStyleSheet("QWidget#leftContent { background: #1E1E1E; border-top-left-radius: 10px; border-bottom-left-radius: 10px; }");
    leftContent->setMouseTracking(true);
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->setContentsMargins(10, 10, 10, 5);
    leftLayout->setSpacing(8);

    m_searchEdit = new SearchLineEdit();

    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background-color: #252526; border: 1px solid #333; border-radius: 6px; "
        "padding: 4px 15px; color: #eee; font-size: 12px; } "
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    m_searchEdit->setPlaceholderText("搜索灵感 (双击查看历史)");
    m_searchEdit->setClearButtonEnabled(true);
    leftLayout->addWidget(m_searchEdit);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(4);
    m_splitter->setChildrenCollapsible(false);

    
    m_listWrapper = new QWidget();
    auto* listWrapperLayout = new QVBoxLayout(m_listWrapper);
    listWrapperLayout->setContentsMargins(0, 0, 0, 0);
    listWrapperLayout->setSpacing(0);

    m_listFocusLine = new QWidget();
    m_listFocusLine->setFixedHeight(1);
    m_listFocusLine->setStyleSheet("background-color: #3498db;");
    m_listFocusLine->hide();
    listWrapperLayout->addWidget(m_listFocusLine);

    m_listStack = new QStackedWidget();
    m_listStack->setMinimumWidth(95); 
    listWrapperLayout->addWidget(m_listStack);

    m_listView = new CleanListView();
    m_listView->setDragEnabled(true);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setIconSize(QSize(28, 28));
    m_listView->setAlternatingRowColors(true);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setMouseTracking(true);
    m_listView->setItemDelegate(new QuickNoteDelegate(this));
    m_model = new NoteModel(this);
    m_listView->setModel(m_model);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);

    connect(m_listView, &CleanListView::internalMoveRequested, this, [this](const QList<int>& ids, int row){
        if (m_currentFilterType == "recently_visited" || m_currentFilterType == "trash") {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 当前视图不支持手动排序</b>");
            return;
        }
        DatabaseManager::instance().moveNotesToRow(ids, row, m_currentFilterType, m_currentFilterValue);
    });

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_lockWidget = new CategoryLockWidget(this);
    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](){
        refreshData();
    });
    connect(m_lockWidget, &CategoryLockWidget::escPressed, this, [this](){
        this->setFocus();
    });
    connect(m_listView, &QListView::customContextMenuRequested, this, &QuickWindow::showListContextMenu);
    connect(m_listView, &QListView::doubleClicked, this, [this](const QModelIndex& index){
        activateNote(index);
    });

    
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, [this](const QItemSelection& selected) {
        if (!selected.isEmpty() && m_listView->hasFocus()) {
            m_bottomStackedWidget->setCurrentIndex(1);
        }
    });
    connect(m_listView, &QListView::clicked, [this](){
         m_bottomStackedWidget->setCurrentIndex(1);
    });

    m_sidebarWrapper = new QWidget();
    m_sidebarWrapper->setMinimumWidth(163); 
    auto* sidebarLayout = new QVBoxLayout(m_sidebarWrapper);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    m_sidebarFocusLine = new QWidget();
    m_sidebarFocusLine->setFixedHeight(1);
    m_sidebarFocusLine->setStyleSheet("background-color: #3498db;");
    m_sidebarFocusLine->hide();
    sidebarLayout->addWidget(m_sidebarFocusLine);

    QString treeStyle = R"(
        QTreeView {
            background-color: transparent;
            border: none;
            outline: none;
            color: #ccc;
        }
        
        QTreeView::item:!selectable {
            color: #ffffff;
            font-weight: bold;
        }
        QTreeView::item {
            height: 22px;
            padding: 0px;
            border: none;
            background: transparent;
        }
        QTreeView::item:hover, QTreeView::item:selected, QTreeView::item:selected:!focus {
            background: transparent;
        }
        QTreeView::branch:hover, QTreeView::branch:selected {
            background: transparent;
        }
        QTreeView::branch:has-children:closed { image: url(:/icons/arrow_right.svg); }
        QTreeView::branch:has-children:open   { image: url(:/icons/arrow_down.svg); }
    )";

    m_systemTree = new DropTreeView();
    m_systemTree->setStyleSheet(treeStyle);
    m_systemTree->setItemDelegate(new CategoryDelegate(this));
    m_systemModel = new CategoryModel(CategoryModel::System, this);

    m_systemProxyModel = new QSortFilterProxyModel(this);
    m_systemProxyModel->setSourceModel(m_systemModel);
    m_systemProxyModel->setFilterRole(CategoryModel::NameRole);
    m_systemProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_systemProxyModel->setRecursiveFilteringEnabled(true);

    m_systemTree->setModel(m_systemProxyModel);
    m_systemTree->setHeaderHidden(true);
    m_systemTree->setMouseTracking(true);

    m_systemTree->setRootIsDecorated(true);
    m_systemTree->setIndentation(12);
    m_systemTree->setFixedHeight(176); // 8 items * 22px = 176px
    m_systemTree->setEditTriggers(QAbstractItemView::NoEditTriggers); 
    m_systemTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_systemTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_systemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_systemTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);

    m_partitionTree = new DropTreeView();
    m_partitionTree->setStyleSheet(treeStyle);
    m_partitionTree->setItemDelegate(new CategoryDelegate(this));
    m_partitionModel = new CategoryModel(CategoryModel::User, this);

    m_partitionProxyModel = new QSortFilterProxyModel(this);
    m_partitionProxyModel->setSourceModel(m_partitionModel);
    m_partitionProxyModel->setFilterRole(CategoryModel::NameRole);
    m_partitionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_partitionProxyModel->setRecursiveFilteringEnabled(true);

    m_partitionTree->setModel(m_partitionProxyModel);
    m_partitionTree->setHeaderHidden(true);
    m_partitionTree->setMouseTracking(true);
    
    m_partitionTree->setRootIsDecorated(true);
    m_partitionTree->setIndentation(12);

    m_partitionTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_partitionTree->setDragEnabled(true);
    m_partitionTree->setAcceptDrops(true);
    m_partitionTree->setDropIndicatorShown(true);
    m_partitionTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_partitionTree->setDefaultDropAction(Qt::MoveAction);
    safeExpandPartitionTree();
    m_partitionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_partitionTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partitionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_partitionTree, &QTreeView::customContextMenuRequested, this, &QuickWindow::showSidebarMenu);
    
    

    
    auto* sbContent = new QWidget();
    sbContent->setAttribute(Qt::WA_StyledBackground, true);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(7, 10, 0, 0);
    sbContentLayout->setSpacing(0);

    sbContentLayout->addWidget(m_systemTree);
    sbContentLayout->addWidget(m_partitionTree);
    sidebarLayout->addWidget(sbContent);

    
    auto onSelectionChanged = [this](DropTreeView* tree, const QModelIndex& index) {
        if (!index.isValid()) return;

        if (tree == m_systemTree) {
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());
        } else {
            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
        }

        m_currentFilterType = index.data(CategoryModel::TypeRole).toString();
        QString name = index.data(CategoryModel::NameRole).toString();
        updatePartitionStatus(name);

        
        m_currentCategoryColor = index.data(CategoryModel::ColorRole).toString();
        if (m_currentCategoryColor.isEmpty()) m_currentCategoryColor = "#4a90e2";

        if (m_currentFilterType == "category") {
            m_currentFilterValue = index.data(CategoryModel::IdRole).toInt();
            StringUtils::recordRecentCategory(m_currentFilterValue.toInt());
            // 2026-05-16 按照用户要求：切断非法绑定。左键点击分类仅用于查看，不再自动改变归档目标。
            // DatabaseManager::instance().setActiveCategoryId(m_currentFilterValue.toInt());
        } else {
            m_currentFilterValue = -1;
            // DatabaseManager::instance().setActiveCategoryId(-1);
        }

        applyListTheme(m_currentCategoryColor);
        m_currentPage = 1;
        refreshData();

        
        m_bottomStackedWidget->setCurrentIndex(0);
    };

    connect(m_systemTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_systemTree, idx); });
    connect(m_partitionTree, &QTreeView::clicked, this, [this, onSelectionChanged](const QModelIndex& idx){ onSelectionChanged(m_partitionTree, idx); });

    
    auto onExpanded = [this](const QModelIndex& index) {
        auto* tree = qobject_cast<QTreeView*>(sender());
        if (!tree) return;

        int catId = index.data(CategoryModel::IdRole).toInt();
        if (catId > 0 && DatabaseManager::instance().isCategoryLocked(catId)) {
            
            tree->collapse(index);
        }
    };
    connect(m_systemTree, &QTreeView::expanded, this, onExpanded);
    connect(m_partitionTree, &QTreeView::expanded, this, onExpanded);

    
    auto onNotesDropped = [this](const QList<int>& ids, const QModelIndex& targetIndex) {
        if (!targetIndex.isValid()) return;
        QString type = targetIndex.data(CategoryModel::TypeRole).toString();

        if (type == "category") {
            int catId = targetIndex.data(CategoryModel::IdRole).toInt();
            DatabaseManager::instance().moveNotesToCategory(ids, catId);
            ActionRecorder::instance().recordMoveToCategory(catId);
            StringUtils::recordRecentCategory(catId);
        } else if (type == "uncategorized") {
            DatabaseManager::instance().moveNotesToCategory(ids, -1);
            ActionRecorder::instance().recordMoveToCategory(-1);
        } else {
            for (int id : ids) {
                if (type == "bookmark") DatabaseManager::instance().updateNoteState(id, "is_favorite", 1);
                else if (type == "trash") DatabaseManager::instance().updateNoteState(id, "is_deleted", 1);
            }
        }
        
        
    };
    connect(m_systemTree, &DropTreeView::notesDropped, this, onNotesDropped);
    connect(m_partitionTree, &DropTreeView::notesDropped, this, onNotesDropped);

    
    
    

    m_listStack->addWidget(m_listView);
    m_listStack->addWidget(m_lockWidget);

    m_filterWrapper = new QFrame();
    m_filterWrapper->setObjectName("FilterWrapper");

    m_filterWrapper->setMinimumWidth(163);
    m_filterWrapper->setStyleSheet("QFrame#FilterWrapper { background-color: #1e1e1e; border: none; }");
    auto* fwLayout = new QVBoxLayout(m_filterWrapper);
    fwLayout->setContentsMargins(0, 0, 0, 0);
    fwLayout->setSpacing(0);

    m_filterPanel = new FilterPanel(this);
    m_filterPanel->setStyleSheet("background: transparent; border: none;");
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &QuickWindow::refreshData);
    fwLayout->addWidget(m_filterPanel);
    m_filterWrapper->hide();

    
    m_splitter->addWidget(m_listWrapper);
    m_splitter->addWidget(m_filterWrapper);
    m_splitter->addWidget(m_sidebarWrapper);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int pos, int index) {
        Q_UNUSED(pos); Q_UNUSED(index);
        this->updateFocusLines();

        
        if (m_filterWrapper && !m_filterWrapper->isHidden()) {
            int w = m_splitter->sizes().at(1);
            if (w >= 163) m_filterWidth = w;
        }
        if (m_sidebarWrapper && !m_sidebarWrapper->isHidden()) {
            int w = m_splitter->sizes().at(2);
            if (w >= 163) m_sidebarWidth = w;
        }
    });
    m_splitter->setCollapsible(0, false); 
    m_splitter->setCollapsible(1, false); 
    m_splitter->setCollapsible(2, false); 
    m_splitter->setStretchFactor(0, 1);   
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({550, 163, 163});
    leftLayout->addWidget(m_splitter);

    
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(2, 0, 0, 5);
    bottomLayout->setSpacing(10);

    m_statusLabel = new QLabel("当前分类: 全部数据");
    m_statusLabel->setStyleSheet("font-size: 11px; color: #888;");
    m_statusLabel->setFixedHeight(32);
    bottomLayout->addWidget(m_statusLabel);

    
    m_bottomStackedWidget = new QStackedWidget();
    m_bottomStackedWidget->setFixedHeight(32);

    
    m_catSearchEdit = new SearchLineEdit();
    m_catSearchEdit->setHistoryKey("CategoryFilterHistory");
    m_catSearchEdit->setHistoryTitle("分类筛选历史");
    m_catSearchEdit->setPlaceholderText("筛选分类...");
    m_catSearchEdit->setClearButtonEnabled(true);

    
    QAction* filterIconAction = new QAction(IconHelper::getIcon("filter_funnel", "#888"), "", m_catSearchEdit);
    m_catSearchEdit->addAction(filterIconAction, QLineEdit::LeadingPosition);

    m_catSearchEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 6px; "
        "padding: 4px 12px 4px 0px; " 
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4FACFE; background-color: rgba(255, 255, 255, 0.08); }"
    );
    connect(m_catSearchEdit, &QLineEdit::textChanged, this, [this](const QString& text){
        
        m_partitionProxyModel->setFilterFixedString(text);
        safeExpandPartitionTree();
    });

    connect(m_catSearchEdit, &QLineEdit::returnPressed, this, [this](){
        QString text = m_catSearchEdit->text().trimmed();
        if (!text.isEmpty()) {
            m_catSearchEdit->addHistoryEntry(text);
        }
    });

    
    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("输入标签添加... (双击显示历史)");
    m_tagEdit->setStyleSheet(
        "QLineEdit { background-color: rgba(255, 255, 255, 0.05); "
        "border: 1px solid rgba(255, 255, 255, 0.1); "
        "border-radius: 6px; "
        "padding: 6px 12px; "
        "font-size: 12px; "
        "color: #EEE; } "
        "QLineEdit:focus { border-color: #4a90e2; background-color: rgba(255, 255, 255, 0.08); } "
        "QLineEdit:disabled { background-color: transparent; border: 1px solid #333; color: #666; }"
    );
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &QuickWindow::handleTagInput);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, [this](){
        this->openTagSelector();
    });

    m_bottomStackedWidget->addWidget(m_catSearchEdit); 
    m_bottomStackedWidget->addWidget(m_tagEdit);       

    bottomLayout->addWidget(m_bottomStackedWidget, 1);
    
    // 2026-05-xx [USER_REQUEST] 按照用户要求：调整底部输入框宽度，右侧边距设为 0 以对齐搜索框
    leftLayout->addLayout(bottomLayout);

    containerLayout->addWidget(leftContent);

    

    QWidget* customToolbar = new QWidget(this);

    customToolbar->setObjectName("customToolbar");
    customToolbar->setMouseTracking(true);
    customToolbar->setAttribute(Qt::WA_Hover);
    customToolbar->setFixedWidth(36); 
    customToolbar->setStyleSheet(
        "QWidget { background-color: #252526; border-top-right-radius: 10px; border-bottom-right-radius: 10px; border-left: 1px solid #333; }"
        "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 0px; margin: 0px; outline: none; }"
        "QPushButton:hover { background-color: #3e3e42; }"
        "QPushButton#btnClose { background-color: #E81123; }"
        "QPushButton#btnClose:hover { background-color: #D71520; }"
        "QPushButton:pressed { background-color: #2d2d2d; }"
        "QLabel { color: #888; font-size: 11px; }"
        "QLineEdit { background: transparent; border: 1px solid #444; border-radius: 4px; color: white; font-size: 11px; font-weight: bold; padding: 0; }"
    );

    QVBoxLayout* toolLayout = new QVBoxLayout(customToolbar);
    toolLayout->setContentsMargins(4, 8, 4, 8); 

    
    toolLayout->setSpacing(0);

    
    auto getScHint = [](const QString& id) -> QString {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        if (seq.isEmpty()) return "";
        
        QString keyText = seq.toString(QKeySequence::NativeText);
        keyText.replace("+", " + ");
        return QString(" （%1）").arg(keyText);
    };

    
    auto createToolBtn = [this, getScHint](QString iconName, QString color, QString tooltip, QString scId = "", int rotate = 0) {
        QPushButton* btn = new QPushButton();
        QIcon icon = IconHelper::getIcon(iconName, color);
        if (rotate != 0) {
            QPixmap pix = icon.pixmap(28, 28);
            QTransform trans;
            trans.rotate(rotate);
            btn->setIcon(QIcon(pix.transformed(trans, Qt::SmoothTransformation)));
        } else {
            btn->setIcon(icon);
        }
        btn->setIconSize(QSize(18, 18)); 
        btn->setFixedSize(24, 24);

        
        QString fullTip = tooltip;
        if (!scId.isEmpty()) fullTip += getScHint(scId);
        btn->setProperty("tooltipText", fullTip);

        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);

        btn->setMouseTracking(true);
        btn->setAttribute(Qt::WA_Hover);
        btn->installEventFilter(this);
        return btn;
    };

    

    

    QPushButton* btnClose = createToolBtn("close", "#FFFFFF", "关闭", "qw_close");
    btnClose->setObjectName("btnClose");
    connect(btnClose, &QPushButton::clicked, this, &QuickWindow::hide);

    m_btnMax = createToolBtn("maximize", "#aaaaaa", "最大化");
    m_btnMax->setProperty("tooltipText", "最大化"); m_btnMax->installEventFilter(this);
    m_btnMax->setObjectName("btnMax");
    connect(m_btnMax, &QPushButton::clicked, [this]() {
        if (this->isMaximized()) {
            this->showNormal();
        } else {
            this->showMaximized();
        }
    });

    
    QPushButton* btnMin = createToolBtn("minimize", "#aaaaaa", "最小化");
    btnMin->setProperty("tooltipText", "最小化"); btnMin->installEventFilter(this);
    btnMin->setObjectName("btnMin");
    connect(btnMin, &QPushButton::clicked, this, &QuickWindow::showMinimized);

    
    QPushButton* btnPin = createToolBtn("pin_tilted", "#aaaaaa", "置顶", "qw_stay_on_top");
    btnPin->setCheckable(true);
    btnPin->setObjectName("btnPin");

    btnPin->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    if (windowFlags() & Qt::WindowStaysOnTopHint) {
        btnPin->setChecked(true);
        btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C"));
    }
    connect(btnPin, &QPushButton::toggled, this, &QuickWindow::toggleStayOnTop);

    
    QPushButton* btnAdd = createToolBtn("add", "#aaaaaa", "新建数据", "qw_new_idea");
    btnAdd->setObjectName("btnAdd");
    btnAdd->setStyleSheet("QPushButton::menu-indicator { width: 0px; image: none; }");

    QMenu* addMenu = new QMenu(this);
    IconHelper::setupMenu(addMenu);
    addMenu->setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");

    addMenu->addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建数据", [this](){
        this->doNewIdea();
    });

    QMenu* createByLineMenu = addMenu->addMenu(IconHelper::getIcon("list_ul", "#aaaaaa", 18), "按行创建数据");
    createByLineMenu->setStyleSheet(addMenu->styleSheet());
    createByLineMenu->addAction("从复制的内容创建", [this](){
        this->doCreateByLine(true);
    });
    createByLineMenu->addAction("从选中数据创建", [this](){
        this->doCreateByLine(false);
    });

    btnAdd->setMenu(addMenu);
    connect(btnAdd, &QPushButton::clicked, [btnAdd](){
        btnAdd->showMenu();
    });

    

    QPushButton* btnSidebar = createToolBtn("eye", "#41F2F2", "显示/隐藏侧边栏", "qw_sidebar");
    btnSidebar->setObjectName("btnSidebar");
    btnSidebar->setCheckable(true);
    btnSidebar->setChecked(true);
    btnSidebar->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");

    connect(btnSidebar, &QPushButton::clicked, this, &QuickWindow::toggleSidebar);

    QPushButton* btnFilter = createToolBtn("filter", "#f1c40f", "显示/隐藏高级筛选", "qw_filter");
    btnFilter->setObjectName("btnFilter");
    btnFilter->setCheckable(true);
    btnFilter->setChecked(false);
    btnFilter->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(btnFilter, &QPushButton::clicked, this, &QuickWindow::toggleFilter);

    
    QPushButton* btnRefresh = createToolBtn("refresh", "#aaaaaa", "刷新", "qw_refresh");
    btnRefresh->setObjectName("btnRefresh");
    connect(btnRefresh, &QPushButton::clicked, this, &QuickWindow::refreshData);

    m_btnToggleAll = createToolBtn("sidebar_open_filled", "#3A90FF", "联动显示/隐藏面板", "qw_toggle_all_panels");
    m_btnToggleAll->setObjectName("btnToggleAll");
    m_btnToggleAll->setCheckable(true);
    m_btnToggleAll->setStyleSheet("QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(m_btnToggleAll, &QPushButton::clicked, this, &QuickWindow::toggleAllPanels);

    m_btnAutoCat = createToolBtn("clipboard_auto", "#aaaaaa", "剪贴板自动归档到当前分类");
    m_btnAutoCat->setObjectName("btnAutoCat");
    m_btnAutoCat->setCheckable(true);
    m_btnAutoCat->setChecked(DatabaseManager::instance().isAutoCategorizeEnabled());
    updateAutoCategorizeButton();
    connect(m_btnAutoCat, &QPushButton::clicked, this, [this](bool checked){
        auto& db = DatabaseManager::instance();
        db.setAutoCategorizeEnabled(checked);
        if (checked) {
            int catId = db.activeCategoryId();
            QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "全部数据";
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #00A650;'>[OK] 自动归档已开启 （%1）</b>").arg(catName));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[OFF] 自动归档已关闭</b>");
        }
    });

    QPushButton* btnLock = createToolBtn("lock_secure", "#aaaaaa", "锁定应用", "qw_lock_app");
    btnLock->setObjectName("btnLock");
    connect(btnLock, &QPushButton::clicked, this, &QuickWindow::doGlobalLock);

    toolLayout->addWidget(btnClose, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnMax, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnMin, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnPin, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnAdd, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);

    
    toolLayout->addWidget(btnSidebar, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnFilter, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnToggleAll, 0, Qt::AlignHCenter); 
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnRefresh, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(m_btnAutoCat, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);
    toolLayout->addSpacing(4);
    toolLayout->addWidget(btnLock, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);

    m_btnNavDir = createToolBtn("nav_first", "#aaaaaa", "切换布局方向");
    m_btnNavDir->setObjectName("btnNavDir");
    connect(m_btnNavDir, &QPushButton::clicked, this, &QuickWindow::toggleLayoutDirection);
    toolLayout->addWidget(m_btnNavDir, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(4);

    toolLayout->addStretch();

    
    QPushButton* btnPrev = createToolBtn("nav_prev", "#aaaaaa", "上一页", "qw_prev_page", 90);
    btnPrev->setObjectName("btnPrev");
    btnPrev->setFixedSize(28, 20);
    connect(btnPrev, &QPushButton::clicked, [this](){
        if (m_currentPage > 1) { m_currentPage--; refreshData(); }
    });

    m_pageInput = new QLineEdit("1");
    m_pageInput->setObjectName("pageInput");
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setFixedSize(24, 20);
    m_pageInput->installEventFilter(this);
    connect(m_pageInput, &QLineEdit::returnPressed, [this](){
        int p = m_pageInput->text().toInt();
        if (p > 0 && p <= m_totalPages) { m_currentPage = p; refreshData(); }
    });

    QLabel* totalLabel = new QLabel("1");
    totalLabel->setObjectName("totalLabel");
    totalLabel->setAlignment(Qt::AlignCenter);
    totalLabel->setStyleSheet("color: #666; font-size: 10px; border: none; background: transparent;");

    QPushButton* btnNext = createToolBtn("nav_next", "#aaaaaa", "下一页", "qw_next_page", 90);
    btnNext->setObjectName("btnNext");
    btnNext->setFixedSize(28, 20);
    connect(btnNext, &QPushButton::clicked, [this](){
        if (m_currentPage < m_totalPages) { m_currentPage++; refreshData(); }
    });

    toolLayout->addWidget(btnPrev, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(m_pageInput, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(totalLabel, 0, Qt::AlignHCenter);
    toolLayout->addSpacing(6);
    toolLayout->addWidget(btnNext, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(20); 

    
    QLabel* verticalTitle = new QLabel("懒\n人\n笔\n记");
    verticalTitle->setAlignment(Qt::AlignCenter);
    verticalTitle->setStyleSheet("color: #444; font-size: 11px; font-weight: bold; border: none; background: transparent; line-height: 1.1;");
    toolLayout->addWidget(verticalTitle, 0, Qt::AlignHCenter);

    toolLayout->addSpacing(12);

    
    QPushButton* btnLogo = createToolBtn("zap", "#3A90FF", "RapidNotes");

    btnLogo->setFixedSize(28, 28);
    btnLogo->setCursor(Qt::ArrowCursor);
    btnLogo->setStyleSheet("background: transparent; border: none;");
    toolLayout->addWidget(btnLogo, 0, Qt::AlignHCenter);

    containerLayout->addWidget(customToolbar);

    
    

    m_mainLayout->addWidget(container);

    
    QTimer::singleShot(500, []() {
        QuickPreview::instance();
    });

    setMinimumSize(400, 700);

    auto* preview = QuickPreview::instance();
    connect(preview, &QuickPreview::editRequested, this, [this](int id){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        this->doEditNote(id);
    });
    connect(preview, &QuickPreview::prevRequested, this, [this](){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_model->rowCount();

        for (int i = 1; i <= count; ++i) {
            int prevRow = (row - i + count) % count;
            QModelIndex idx = m_model->index(prevRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                updatePreviewContent();
                if (prevRow > row) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至列表末尾相同分类");
                }
                return;
            }
        }
    });
    connect(preview, &QuickPreview::nextRequested, this, [this](){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        QModelIndex current = m_listView->currentIndex();
        if (!current.isValid() || m_model->rowCount() == 0) return;

        int catId = current.data(NoteModel::CategoryIdRole).toInt();
        int row = current.row();
        int count = m_model->rowCount();

        for (int i = 1; i <= count; ++i) {
            int nextRow = (row + i) % count;
            QModelIndex idx = m_model->index(nextRow, 0);
            if (idx.data(NoteModel::CategoryIdRole).toInt() == catId) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                updatePreviewContent();
                if (nextRow < row) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至列表起始相同分类");
                }
                return;
            }
        }
    });
    connect(preview, &QuickPreview::historyNavigationRequested, this, [this](int id){
        auto* preview = QuickPreview::instance();
        if (!preview->caller() || preview->caller()->window() != this) return;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            QModelIndex idx = m_model->index(i, 0);
            if (idx.data(NoteModel::IdRole).toInt() == id) {
                m_listView->setCurrentIndex(idx);
                m_listView->scrollTo(idx);
                return;
            }
        }
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        if (!note.isEmpty()) {
            preview->showPreview(note, preview->pos(), "", m_listView);
        }
    });
    m_listView->installEventFilter(this);
    m_systemTree->installEventFilter(this);
    m_partitionTree->installEventFilter(this);
    m_searchEdit->installEventFilter(this);
    m_catSearchEdit->installEventFilter(this);
    m_tagEdit->installEventFilter(this);

    
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &QuickWindow::refreshData);
    connect(m_searchEdit, &QLineEdit::textChanged, [this](const QString& text){
        m_currentPage = 1;
        m_searchTimer->start(300);
    });

    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){
        QString text = m_searchEdit->text().trimmed();
        if (text.isEmpty()) return;
        m_searchEdit->addHistoryEntry(text);

        
        m_searchTimer->stop();
        refreshData();
    });

    
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](){
        auto selected = m_listView->selectionModel()->selectedIndexes();
        if (selected.isEmpty()) {
            
            m_bottomStackedWidget->setCurrentIndex(0);
            m_tagEdit->setEnabled(false);
        } else {
            
            
            if (m_listView->hasFocus()) {
                m_bottomStackedWidget->setCurrentIndex(1);
            }

            m_tagEdit->setEnabled(true);
            m_tagEdit->setPlaceholderText(selected.size() == 1 ? "标签... (双击显示历史)" : "批量添加标签... (双击显示历史)");

            
            auto* preview = QuickPreview::instance();
            if (preview->isVisible()) {
                updatePreviewContent();
            }
        }
    });

    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &QuickWindow::updateShortcuts);
    updateAppLockStatus();
    restoreState();

    updateLayoutWidth();
    updateToggleAllIcon();
    refreshData();
    applyListTheme("");
    updateShortcuts();
    setupAppLock();

    m_idleLockTimer = new QTimer(this);
    m_idleLockTimer->setInterval(5000); 
    connect(m_idleLockTimer, &QTimer::timeout, this, &QuickWindow::checkIdleLock);
    m_idleLockTimer->start();
}

void QuickWindow::setupAppLock() {
    if (m_appLockWidget) return;
    QSettings settings("RapidNotes", "QuickWindow");
    QString appPwd = settings.value("appPassword").toString();
    if (!appPwd.isEmpty()) {
        auto* lock = new AppLockWidget(appPwd, this);
        m_appLockWidget = lock;
        lock->resize(this->size());

        connect(lock, &AppLockWidget::unlocked, this, [this]() {
            m_appLockWidget = nullptr;
            updateAppLockStatus();

            m_listView->setFocus();
            if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
                m_listView->setCurrentIndex(m_model->index(0, 0));
            }
        });

        lock->show();
        lock->raise();
    }
}

void QuickWindow::saveState() {
    QSettings settings("RapidNotes", "QuickWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitter", m_splitter->saveState());
    settings.setValue("sidebarHidden", m_sidebarWrapper->isHidden());
    settings.setValue("filterHidden", m_filterWrapper->isHidden());
    settings.setValue("stayOnTop", m_isStayOnTop);
    settings.setValue("sidebarWidth", m_sidebarWidth);
    settings.setValue("filterWidth", m_filterWidth);
}

void QuickWindow::restoreState() {
    QSettings settings("RapidNotes", "QuickWindow");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitter")) {
        m_splitter->restoreState(settings.value("splitter").toByteArray());
    }

    m_sidebarWidth = settings.value("sidebarWidth", 163).toInt();
    m_filterWidth = settings.value("filterWidth", 163).toInt();
    if (m_sidebarWidth < 163) m_sidebarWidth = 163;
    if (m_filterWidth < 163) m_filterWidth = 163;
    if (settings.contains("sidebarHidden")) {
        bool hidden = settings.value("sidebarHidden").toBool();
        m_sidebarWrapper->setHidden(hidden);

        
        auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
        if (btnSidebar) {
            bool visible = !hidden;
            btnSidebar->setChecked(visible);

            btnSidebar->setIcon(IconHelper::getIcon("eye", "#41F2F2"));
        }
    }
    if (settings.contains("filterHidden")) {
        bool hidden = settings.value("filterHidden").toBool();
        m_filterWrapper->setHidden(hidden);

        
        auto* btnFilter = findChild<QPushButton*>("btnFilter");
        if (btnFilter) {
            bool visible = !hidden;
            btnFilter->setChecked(visible);
            btnFilter->setIcon(IconHelper::getIcon("filter", "#f1c40f"));
        }
    }
    if (settings.contains("stayOnTop")) {
        toggleStayOnTop(settings.value("stayOnTop").toBool());
    }
}

void QuickWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) {
        auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
        sc->setProperty("id", id);
        m_shortcuts.append(sc);
    };

    add("qw_search", [this](){ m_searchEdit->setFocus(); m_searchEdit->selectAll(); });

    
    auto* delSoftSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_delete_soft"), m_listView, [this](){ doDeleteSelected(false); }, Qt::WidgetShortcut);
    delSoftSc->setProperty("id", "qw_delete_soft");
    m_shortcuts.append(delSoftSc);
    auto* delHardSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_delete_hard"), m_listView, [this](){ doDeleteSelected(true); }, Qt::WidgetShortcut);
    delHardSc->setProperty("id", "qw_delete_hard");
    m_shortcuts.append(delHardSc);

    add("qw_favorite", [this](){ doToggleFavorite(); });
    
    auto* previewSc = new QShortcut(ShortcutManager::instance().getShortcut("qw_preview"), m_listView, [this](){ doPreview(); }, Qt::WidgetShortcut);
    previewSc->setProperty("id", "qw_preview");
    m_shortcuts.append(previewSc);

    add("qw_pin", [this](){ doTogglePin(); });
    add("qw_close", [this](){ hide(); });
    add("qw_new_idea", [this](){ doNewIdea(); });
    add("qw_select_all", [this](){ m_listView->selectAll(); });
    add("qw_extract", [this](){ doExtractContent(); });

    add("qw_lock_cat", [this](){
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
            refreshSidebar();
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已立即锁定</b>");
        }
    });

    add("qw_lock_all_cats", [this](){

        DatabaseManager::instance().lockAllCategories();
        refreshSidebar();
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 所有分类已闪速锁定</b>");
    });

    // add("qw_toggle_locked_visibility", ...);
    add("qw_stay_on_top", [this](){ toggleStayOnTop(!m_isStayOnTop); });

    add("qw_edit", [this](){ doEditSelected(); });
    add("qw_sidebar", [this](){

        toggleSidebar();
    });

    
    add("qw_prev_page", [this](){ if(m_currentPage > 1) { m_currentPage--; refreshData(); } });
    add("qw_next_page", [this](){ if(m_currentPage < m_totalPages) { m_currentPage++; refreshData(); } });
    
    add("qw_refresh", [this](){ refreshData(); });
    add("qw_copy_tags", [this](){ doCopyTags(); });
    add("qw_paste_tags", [this](){ doPasteTags(); });
    add("qw_repeat_action", [this](){ doRepeatAction(); });
    auto showAllLogic = [this](){
        m_currentFilterType = "all";
        m_currentFilterValue = -1;
        m_currentPage = 1;

        
        m_systemTree->selectionModel()->clearSelection();
        m_systemTree->setCurrentIndex(QModelIndex());
        m_partitionTree->selectionModel()->clearSelection();
        m_partitionTree->setCurrentIndex(QModelIndex());

        m_currentCategoryColor = "#4a90e2";
        updatePartitionStatus("");
        applyListTheme("");
        refreshData();

        ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
    };
    
    add("qw_show_all", showAllLogic);
    connect(&KeyboardHook::instance(), &KeyboardHook::showAllDataRequested, this, showAllLogic);

    for (int i = 0; i <= 5; ++i) {
        add(QString("qw_rating_%1").arg(i), [this, i](){ doSetRating(i); });
    }
}

void QuickWindow::updateAutoCategorizeButton() {
    if (!m_btnAutoCat) return;
    auto& db = DatabaseManager::instance();
    bool enabled = db.isAutoCategorizeEnabled();
    m_btnAutoCat->setChecked(enabled);
    m_btnAutoCat->setIcon(IconHelper::getIcon(enabled ? "switch_on" : "switch_off", enabled ? "#00A650" : "#aaaaaa"));

    int catId = db.activeCategoryId();
    QString catName = (catId > 0) ? db.getCategoryNameById(catId) : "全部数据";

    if (enabled) {
        // 2026-05-16 按照用户要求：优化提示语，明确显示归档目标
        m_btnAutoCat->setProperty("tooltipText", QString("自动归档：开启 (目标: %1)").arg(catName)); 
        m_btnAutoCat->installEventFilter(this);
    } else {
        m_btnAutoCat->setProperty("tooltipText", QString("自动归档：关闭 (预设目标: %1)").arg(catName)); 
        m_btnAutoCat->installEventFilter(this);
    }
}

void QuickWindow::updateAppLockStatus() {
    QPushButton* btnLock = findChild<QPushButton*>("btnLock");
    if (!btnLock) return;

    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();

    if (hasPwd) {
        
        btnLock->setIcon(IconHelper::getIcon("unlock_secure", "#00A650"));
    } else {
        
        btnLock->setIcon(IconHelper::getIcon("lock_secure", "#aaaaaa"));
    }
}

void QuickWindow::updateShortcuts() {
    for (auto* sc : m_shortcuts) {
        QString id = sc->property("id").toString();
        sc->setKey(ShortcutManager::instance().getShortcut(id));
    }

    
    auto getScHint = [](const QString& id) -> QString {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        if (seq.isEmpty()) return "";
        QString keyText = seq.toString(QKeySequence::NativeText);
        keyText.replace("+", " + ");
        return QString(" （%1）").arg(keyText);
    };

    auto updateBtnTip = [&](const QString& objName, const QString& baseTip, const QString& scId) {
        QPushButton* btn = findChild<QPushButton*>(objName);
        if (btn) { btn->setProperty("tooltipText", baseTip + getScHint(scId)); btn->installEventFilter(this); }
    };

    updateBtnTip("btnClose", "关闭", "qw_close");
    updateBtnTip("btnPin", "置顶", "qw_stay_on_top");

    updateBtnTip("btnSidebar", "显示/隐藏侧边栏", "qw_sidebar");
    updateBtnTip("btnFilter", "显示/隐藏高级筛选", "qw_filter");
    updateBtnTip("btnToggleAll", "联动显示/隐藏面板", "qw_toggle_all_panels");
    updateBtnTip("btnLock", "锁定应用", "qw_lock_app");
    
    updateBtnTip("btnRefresh", "刷新", "qw_refresh");
    updateBtnTip("btnPrev", "上一页", "qw_prev_page");
    updateBtnTip("btnNext", "下一页", "qw_next_page");
}

void QuickWindow::scheduleRefresh() {
    m_refreshTimer->start();
    
    refreshSidebar();
}

void QuickWindow::onNoteAdded(const QVariantMap& note) {
    
    if (note.value("is_deleted").toInt() == 1) return; 

    
    bool matches = true;
    if (m_currentFilterType == "category") {
        matches = (note.value("category_id").toInt() == m_currentFilterValue.toInt());
    } else if (m_currentFilterType == "untagged") {
        matches = note.value("tags").toString().isEmpty();
    } else if (m_currentFilterType == "bookmark") {
        matches = (note.value("is_favorite").toInt() == 1);
    } else if (m_currentFilterType == "trash") {
        matches = false; 
    }
    

    QString keyword = m_searchEdit->text().trimmed();
    if (matches && !keyword.isEmpty()) {
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QString tags = note.value("tags").toString();
        if (!title.contains(keyword, Qt::CaseInsensitive) &&
            !content.contains(keyword, Qt::CaseInsensitive) &&
            !tags.contains(keyword, Qt::CaseInsensitive)) {
            matches = false;
        }
    }

    if (matches && m_filterWrapper && !m_filterWrapper->isHidden()) {
        matches = false;
    }

    if (matches && m_currentPage == 1) {
        m_model->prependNote(note);
    }

    
    scheduleRefresh();
}

void QuickWindow::refreshData() {
    qDebug() << "[QuickWindow] 开始执行 refreshData()...";
    if (!isVisible()) return;

    
    QSet<int> selectedIds;
    auto selectedIndices = m_listView->selectionModel()->selectedIndexes();
    for (const auto& idx : selectedIndices) {
        selectedIds.insert(idx.data(NoteModel::IdRole).toInt());
    }
    int lastCurrentId = m_listView->currentIndex().data(NoteModel::IdRole).toInt();

    QString keyword = m_searchEdit->text();

    QVariantMap criteria = m_filterPanel->getCheckedCriteria();
    int totalCount = DatabaseManager::instance().getNotesCount(keyword, m_currentFilterType, m_currentFilterValue, criteria);

    const int pageSize = DatabaseManager::DEFAULT_PAGE_SIZE;
    m_totalPages = qMax(1, (totalCount + pageSize - 1) / pageSize);
    if (m_currentPage > m_totalPages) m_currentPage = m_totalPages;
    if (m_currentPage < 1) m_currentPage = 1;

    
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

    m_listStack->setCurrentWidget(isLocked ? static_cast<QWidget*>(m_lockWidget) : static_cast<QWidget*>(m_listView));

    auto* preview = QuickPreview::instance();
    if (isLocked && preview->isVisible() && preview->caller() && preview->caller()->window() == this) {
        preview->hide();
    }

    m_model->setNotes(isLocked ? QList<QVariantMap>() : DatabaseManager::instance().searchNotes(keyword, m_currentFilterType, m_currentFilterValue, m_currentPage, pageSize, criteria));

    
    if (!selectedIds.isEmpty()) {
        QItemSelection selection;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            QModelIndex idx = m_model->index(i, 0);
            int id = idx.data(NoteModel::IdRole).toInt();
            if (selectedIds.contains(id)) {
                selection.select(idx, idx);
            }
            if (id == lastCurrentId) {
                m_listView->setCurrentIndex(idx);
            }
        }
        if (!selection.isEmpty()) {
            m_listView->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    
    auto* pageInput = findChild<QLineEdit*>("pageInput");
    if (pageInput) pageInput->setText(QString::number(m_currentPage));

    auto* totalLabel = findChild<QLabel*>("totalLabel");
    if (totalLabel) totalLabel->setText(QString::number(m_totalPages));

    if (!m_filterWrapper->isHidden()) {
        m_filterPanel->updateStats(keyword, m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::updatePartitionStatus(const QString& name) {
    m_statusLabel->setText(QString("当前分类: %1").arg(name.isEmpty() ? "全部数据" : name));
    m_statusLabel->show();
}

void QuickWindow::refreshSidebar() {
    if (!isVisible()) return;
    
    // 2026-04-18 增强焦点保护：使用 isAncestorOf 确保覆盖重命名时的编辑器 (LineEdit)
    QWidget* focusWidget = qApp->focusWidget();
    bool partitionTreeFocused = (m_partitionTree && (m_partitionTree == focusWidget || m_partitionTree->isAncestorOf(focusWidget)));
    bool systemTreeFocused = (m_systemTree && (m_systemTree == focusWidget || m_systemTree->isAncestorOf(focusWidget)));

    QString selectedType;
    QVariant selectedValue;
    QModelIndex sysIdx = m_systemTree->currentIndex();
    QModelIndex partIdx = m_partitionTree->currentIndex();

    if (sysIdx.isValid()) {
        selectedType = sysIdx.data(CategoryModel::TypeRole).toString();
        selectedValue = sysIdx.data(CategoryModel::NameRole);
    } else if (partIdx.isValid()) {
        // [镜像模式适配]：恢复选中时优先匹配 ID，跳过物理组项
        if (partIdx.data(CategoryModel::NameRole).toString() != "我的分类" && 
            partIdx.data(CategoryModel::NameRole).toString() != "快速访问") {
            selectedType = partIdx.data(CategoryModel::TypeRole).toString();
            selectedValue = partIdx.data(CategoryModel::IdRole);
        }
    }

    m_systemModel->refresh();
    m_partitionModel->refresh();
    safeExpandPartitionTree();

    // 恢复选中状态
    if (!selectedType.isEmpty()) {
        if (selectedType != "category") {
            for (int i = 0; i < m_systemModel->rowCount(); ++i) {
                QModelIndex idx = m_systemModel->index(i, 0);
                if (idx.data(CategoryModel::TypeRole).toString() == selectedType &&
                    idx.data(CategoryModel::NameRole) == selectedValue) {
                    m_systemTree->setCurrentIndex(m_systemProxyModel->mapFromSource(idx));
                    break;
                }
            }
        } else {
            std::function<void(const QModelIndex&)> findAndSelect = [&](const QModelIndex& parent) {
                for (int i = 0; i < m_partitionModel->rowCount(parent); ++i) {
                    QModelIndex idx = m_partitionModel->index(i, 0, parent);
                    if (idx.data(CategoryModel::IdRole) == selectedValue) {
                        m_partitionTree->setCurrentIndex(m_partitionProxyModel->mapFromSource(idx));
                        return;
                    }
                    if (m_partitionModel->rowCount(idx) > 0) findAndSelect(idx);
                }
            };
            findAndSelect(QModelIndex());
        }
    }

    // 焦点回归：如果是侧边栏树失去焦点，强制将其夺回
    if (partitionTreeFocused) m_partitionTree->setFocus();
    else if (systemTreeFocused) m_systemTree->setFocus();
}

void QuickWindow::safeExpandPartitionTree() {

    
    std::function<void(const QModelIndex&)> expandRec = [&](const QModelIndex& parent) {
        for (int i = 0; i < m_partitionProxyModel->rowCount(parent); ++i) {
            QModelIndex idx = m_partitionProxyModel->index(i, 0, parent);
            int catId = idx.data(CategoryModel::IdRole).toInt();

            
            if (catId > 0) {
                if (!DatabaseManager::instance().isCategoryLocked(catId)) {
                    m_partitionTree->setExpanded(idx, true);
                    
                    if (m_partitionProxyModel->rowCount(idx) > 0) expandRec(idx);
                } else {
                    
                    m_partitionTree->setExpanded(idx, false);
                }
            } else {
                
                m_partitionTree->setExpanded(idx, true);
                if (m_partitionProxyModel->rowCount(idx) > 0) expandRec(idx);
            }
        }
    };
    expandRec(QModelIndex());
}

void QuickWindow::applyListTheme(const QString& colorHex) {
    QString style;
    if (!colorHex.isEmpty()) {
        QColor c(colorHex);
        
        style = QString("QListView { "
                        "  border: none; "
                        "  background-color: #1e1e1e; "
                        "  alternate-background-color: #252526; "
                        "  selection-background-color: transparent; "
                        "  color: #eee; "
                        "  outline: none; "
                        "}");
    } else {
        style = "QListView { "
                "  border: none; "
                "  background-color: #1e1e1e; "
                "  alternate-background-color: #252526; "
                "  selection-background-color: transparent; "
                "  color: #eee; "
                "  outline: none; "
                "}";
    }
    m_listView->setStyleSheet(style);
}

void QuickWindow::activateNote(const QModelIndex& index) {
    if (!index.isValid()) return;

    int id = index.data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);

    
    DatabaseManager::instance().recordAccess(id);

    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();

    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else if (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch" ||
               (QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).exists() && QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).isAbsolute())) {

        
        
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        bool isExplicitPath = (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch");

        QString path = isExplicitPath ? content : plainContent;
        QString fullPath = path;
        if (path.startsWith("attachments/")) {
            fullPath = QCoreApplication::applicationDirPath() + "/" + path;
        }

        QFileInfo fi(fullPath);
        if (fi.exists()) {
            QMimeData* mimeData = new QMimeData();
            if (itemType == "local_batch") {
                
                QDir dir(fullPath);
                QList<QUrl> urls;
                for (const QString& fileName : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                    urls << QUrl::fromLocalFile(dir.absoluteFilePath(fileName));
                }
                if (urls.isEmpty()) urls << QUrl::fromLocalFile(fullPath); 
                mimeData->setUrls(urls);
            } else {
                mimeData->setUrls({QUrl::fromLocalFile(fullPath)});
            }
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 文件已丢失或被移动</b>");
        }
    } else if (!blob.isEmpty() && (itemType == "file" || itemType == "folder")) {

        if (!verifyExportPermission()) return;

        
        QString title = note.value("title").toString();
        QString exportDir = QDir::tempPath() + "/RapidNotes_Export";
        QDir().mkpath(exportDir);
        QString tempPath = exportDir + "/" + title;

        QFile f(tempPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(blob);
            f.close();

            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls({QUrl::fromLocalFile(tempPath)});
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
        }
    } else if (itemType != "text" && !itemType.isEmpty()) {
        QStringList rawPaths = content.split(';', Qt::SkipEmptyParts);
        QList<QUrl> validUrls;
        QStringList missingFiles;

        for (const QString& p : std::as_const(rawPaths)) {
            QString path = p.trimmed().remove('\"');
            if (QFileInfo::exists(path)) {
                validUrls << QUrl::fromLocalFile(path);
            } else {
                missingFiles << QFileInfo(path).fileName();
            }
        }

        if (!validUrls.isEmpty()) {
            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls(validUrls);
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            if (!missingFiles.isEmpty()) {

                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 原文件已丢失，已复制路径文本</b>", 700);
            }
        }
    } else {
        StringUtils::copyNoteToClipboard(content);
    }

    

#ifdef Q_OS_WIN
    if (m_lastActiveHwnd && IsWindow(m_lastActiveHwnd)) {
        DWORD currThread = GetCurrentThreadId();
        bool attached = false;
        if (m_lastThreadId != 0 && m_lastThreadId != currThread) {
            attached = AttachThreadInput(currThread, m_lastThreadId, TRUE);
        }

        if (IsIconic(m_lastActiveHwnd)) {
            ShowWindow(m_lastActiveHwnd, SW_RESTORE);
        }
        SetForegroundWindow(m_lastActiveHwnd);

        if (m_lastFocusHwnd && IsWindow(m_lastFocusHwnd)) {
            SetFocus(m_lastFocusHwnd);
        }

        DWORD lastThread = m_lastThreadId;
        QTimer::singleShot(300, [lastThread, attached]() {
            
            
            INPUT releaseInputs[8];
            memset(releaseInputs, 0, sizeof(releaseInputs));
            BYTE keys[] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };
            for (int i = 0; i < 8; ++i) {
                releaseInputs[i].type = INPUT_KEYBOARD;
                releaseInputs[i].ki.wVk = keys[i];
                releaseInputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
            }
            SendInput(8, releaseInputs, sizeof(INPUT));

            
            INPUT inputs[4];
            memset(inputs, 0, sizeof(inputs));

            
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_LCONTROL;
            inputs[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);

            
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = 'V';
            inputs[1].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);

            
            inputs[2].type = INPUT_KEYBOARD;
            inputs[2].ki.wVk = 'V';
            inputs[2].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

            
            inputs[3].type = INPUT_KEYBOARD;
            inputs[3].ki.wVk = VK_LCONTROL;
            inputs[3].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, inputs, sizeof(INPUT));

            if (attached) {
                
                AttachThreadInput(GetCurrentThreadId(), lastThread, FALSE);
            }
        });
    }
#endif
}

void QuickWindow::doDeleteSelected(bool physical) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
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
                refreshSidebar();
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已永久删除 %1 条数据</b>").arg(idsToDelete.size()));
            }
        }
    } else {
        
        QList<int> idsToTrash;
        for (const auto& index : std::as_const(selected)) idsToTrash << index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().softDeleteNotes(idsToTrash);
        refreshData();
    }
    refreshSidebar();
}

void QuickWindow::doRestoreTrash() {
    if (DatabaseManager::instance().restoreAllFromTrash()) {
        refreshData();
        refreshSidebar();
    }
}

void QuickWindow::doToggleFavorite() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    bool isFav = false;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        isFav = DatabaseManager::instance().toggleNoteState(id, "is_favorite");
    }
    refreshData();
    
    // 2026-04-xx 按照用户要求：增加显式反馈，并提升优先级以防被覆盖
    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        isFav ? "<b style='color: #2ecc71;'>★ 已添加至收藏</b>" : "<b style='color: #aaaaaa;'>☆ 已取消收藏</b>", 
        1000, QColor("#B0B0B0"), ToolTipOverlay::ActivePriority);
}

void QuickWindow::doTogglePin() {
    QWidget* focus = QApplication::focusWidget();

    
    if (focus == m_systemTree || focus == m_partitionTree) {
        QModelIndex index = (focus == m_systemTree) ? m_systemTree->currentIndex() : m_partitionTree->currentIndex();
        if (index.isValid()) {
            
            QModelIndex srcIdx;
            if (focus == m_systemTree) srcIdx = m_systemProxyModel->mapToSource(index);
            else srcIdx = m_partitionProxyModel->mapToSource(index);

            if (srcIdx.isValid() && srcIdx.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = srcIdx.data(CategoryModel::IdRole).toInt();
                DatabaseManager::instance().toggleCategoryPinned(catId);
                refreshSidebar();
                return;
            }
        }
    }

    
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    bool isPinned = false;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        isPinned = DatabaseManager::instance().toggleNoteState(id, "is_pinned");
    }
    refreshData();

    // 2026-04-xx 按照用户要求：增加显式反馈，并提升优先级以防被覆盖
    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        isPinned ? "<b style='color: #2ecc71;'>[OK] 灵感已置顶</b>" : "<b style='color: #aaaaaa;'>[OFF] 灵感已取消置顶</b>", 
        1000, QColor("#B0B0B0"), ToolTipOverlay::ActivePriority);
}

void QuickWindow::doNewIdea() {
    
    NoteEditWindow* win = new NoteEditWindow();
    int catId = getCurrentCategoryId();
    if (catId > 0) {
        win->setDefaultCategory(catId);
    }
    connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
    win->show();
}

void QuickWindow::doMergeSelected() {

    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.size() < 2) return;

    QString firstTitle = selected.first().data(NoteModel::IdRole).toInt() > 0 ?
                         selected.first().data(NoteModel::TitleRole).toString() : "合并后的灵感";
    QString finalTitle = firstTitle + " (合并)";

    QStringList mergedContents;
    QSet<QString> tagsSet;

    for (const auto& index : std::as_const(selected)) {
        QString content = index.data(NoteModel::ContentRole).toString();
        mergedContents << StringUtils::htmlToPlainText(content).trimmed();

        QString tags = index.data(NoteModel::TagsRole).toString();
        QStringList parts = tags.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
        for (const QString& t : parts) tagsSet.insert(t.trimmed());
    }

    QString finalContent = mergedContents.join("\n\n");
    QStringList finalTags = tagsSet.values();
    int catId = getCurrentCategoryId();

    DatabaseManager::instance().addNote(finalTitle, finalContent, finalTags, "", catId);

    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已成功合并 %1 条数据并直接入库</b>").arg(selected.size()));
}

void QuickWindow::doCreateByLine(bool fromClipboard) {
    QString text;
    if (fromClipboard) {
        text = QApplication::clipboard()->text();
    } else {
        auto selected = m_listView->selectionModel()->selectedIndexes();
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

void QuickWindow::doExtractContent() {

    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QList<QVariantMap> notes;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        
        DatabaseManager::instance().recordAccess(id);
        notes << DatabaseManager::instance().getNoteById(id);
    }

    StringUtils::copyNotesToClipboard(notes);
    
    // [FIX] 2026-04-xx 按照用户要求：提升反馈优先级为 ActivePriority。
    // 物理拦截任何随后产生的按钮悬浮提示（PassivePriority），确保用户能清晰看到操作结果。
    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        QString("<b style='color: #2ecc71;'>[OK] 已成功提取 %1 条数据内容</b>").arg(notes.size()), 1500, QColor("#B0B0B0"), ToolTipOverlay::ActivePriority);
}

void QuickWindow::doEditSelected() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;
    doEditNote(index.data(NoteModel::IdRole).toInt());
}

void QuickWindow::doEditNote(int id) {
    if (id <= 0) return;
    NoteEditWindow* win = new NoteEditWindow(id);
    connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
    win->show();
}

void QuickWindow::doSetRating(int rating) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().updateNoteState(id, "rating", rating);
    }
    refreshData();
}

void QuickWindow::doGlobalLock() {
    
    QSettings settings("RapidNotes", "QuickWindow");
    if (settings.value("appPassword").toString().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 尚未设定应用密码，请先进行设定</b>");
        return;
    }

    
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget == this) continue;
        if (widget->inherits("QSystemTrayIcon")) continue;

        
        if (widget->isVisible()) {
            widget->hide();
        }
    }

    
    setupAppLock();

    
    showAuto();

    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 应用已锁定</b>");
}

void QuickWindow::updatePreviewContent() {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    
    QVariantMap note;
    note["id"] = index.data(NoteModel::IdRole);
    note["title"] = index.data(NoteModel::TitleRole);
    note["content"] = index.data(NoteModel::ContentRole);
    note["item_type"] = index.data(NoteModel::TypeRole);
    note["data_blob"] = index.data(NoteModel::BlobRole);
    note["tags"] = index.data(NoteModel::TagsRole);
    note["rating"] = index.data(NoteModel::RatingRole);
    note["is_pinned"] = index.data(NoteModel::PinnedRole);
    note["is_favorite"] = index.data(NoteModel::FavoriteRole);
    note["created_at"] = index.data(NoteModel::TimeRole);
    note["updated_at"] = index.data(NoteModel::TimeRole); 
    note["remark"] = index.data(NoteModel::RemarkRole);

    QString catName = index.data(NoteModel::CategoryNameRole).toString();

    auto* preview = QuickPreview::instance();
    QPoint pos = preview->isVisible() ? preview->pos() : m_listView->mapToGlobal(m_listView->rect().center()) - QPoint(250, 300);

    preview->showPreview(note, pos, catName, m_listView);
}

void QuickWindow::doPreview() {
    
    static QElapsedTimer timer;
    if (timer.isValid() && timer.elapsed() < 200) {
        return;
    }
    timer.restart();

    auto* preview = QuickPreview::instance();

    QWidget* focusWidget = QApplication::focusWidget();
    
    if (focusWidget) {
        bool isInput = qobject_cast<QLineEdit*>(focusWidget) ||
                       qobject_cast<QTextEdit*>(focusWidget) ||
                       qobject_cast<QPlainTextEdit*>(focusWidget);

        if (isInput) {
            bool isReadOnly = focusWidget->property("readOnly").toBool();
            if (auto* le = qobject_cast<QLineEdit*>(focusWidget)) isReadOnly = le->isReadOnly();

            if (!isReadOnly) return;
        }
    }

    
    if (preview->isVisible() && preview->caller() && preview->caller()->window() == this) {
        preview->hide();
        return;
    }

    updatePreviewContent();

    preview->raise();
    preview->activateWindow();
}

void QuickWindow::toggleStayOnTop(bool checked) {
    m_isStayOnTop = checked;

    if (isVisible()) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        Qt::WindowFlags f = windowFlags();
        if (checked) f |= Qt::WindowStaysOnTopHint;
        else f &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
#endif
    }
    
    auto* btnPin = findChild<QPushButton*>("btnPin");
    if (btnPin) {
        if (btnPin->isChecked() != checked) btnPin->setChecked(checked);

        btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa", 20));
    }
}

void QuickWindow::toggleSidebar() {

    if (!m_sidebarWrapper) return;

    bool visible = !m_sidebarWrapper->isVisible();
    m_sidebarWrapper->setVisible(visible);

    
    auto* btnSidebar = findChild<QPushButton*>("btnSidebar");
    if (btnSidebar) {
        btnSidebar->setChecked(visible);

        btnSidebar->setIcon(IconHelper::getIcon("eye", "#41F2F2"));
    }

    
    updateLayoutWidth();
    updateToggleAllIcon(); 

    QString name;
    if (m_systemTree->currentIndex().isValid()) name = m_systemTree->currentIndex().data().toString();
    else name = m_partitionTree->currentIndex().data().toString();

    updatePartitionStatus(name);
    updateFocusLines();
}

void QuickWindow::toggleFilter() {

    if (!m_filterWrapper) return;

    bool visible = !m_filterWrapper->isVisible();
    m_filterWrapper->setVisible(visible);

    
    updateLayoutWidth();
    updateToggleAllIcon(); 

    
    QPushButton* btnFilter = findChild<QPushButton*>("btnFilter");
    if (btnFilter) {
        btnFilter->setChecked(visible);

        btnFilter->setIcon(IconHelper::getIcon("filter", "#f1c40f"));
    }

    if (visible && m_filterPanel) {
        m_filterPanel->updateStats(m_searchEdit->text(), m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::toggleLayoutDirection() {
    m_isLayoutReversed = !m_isLayoutReversed;
    
    if (m_btnNavDir) {
        m_btnNavDir->setIcon(IconHelper::getIcon(m_isLayoutReversed ? "nav_last" : "nav_first", "#aaaaaa"));
    }

    QWidget* container = findChild<QWidget*>("container");
    if (!container) return;
    QHBoxLayout* containerLayout = qobject_cast<QHBoxLayout*>(container->layout());
    if (!containerLayout) return;

    QWidget* leftContent = findChild<QWidget*>("leftContent");
    QWidget* customToolbar = findChild<QWidget*>("customToolbar");

    if (leftContent && customToolbar) {
        containerLayout->removeWidget(leftContent);
        containerLayout->removeWidget(customToolbar);
        if (m_isLayoutReversed) {
            containerLayout->addWidget(customToolbar);
            containerLayout->addWidget(leftContent);
            leftContent->setStyleSheet("QWidget#leftContent { background: #1E1E1E; border-top-right-radius: 10px; border-bottom-right-radius: 10px; }");
        } else {
            containerLayout->addWidget(leftContent);
            containerLayout->addWidget(customToolbar);
            leftContent->setStyleSheet("QWidget#leftContent { background: #1E1E1E; border-top-left-radius: 10px; border-bottom-left-radius: 10px; }");
        }
    }

    if (m_splitter && m_listWrapper && m_filterWrapper && m_sidebarWrapper) {
        if (m_isLayoutReversed) {
            m_splitter->insertWidget(0, m_sidebarWrapper);
            m_splitter->insertWidget(1, m_filterWrapper);
            m_splitter->insertWidget(2, m_listWrapper);
        } else {
            m_splitter->insertWidget(0, m_listWrapper);
            m_splitter->insertWidget(1, m_filterWrapper);
            m_splitter->insertWidget(2, m_sidebarWrapper);
        }
    }
}

void QuickWindow::toggleAllPanels() {

    bool sidebarVisible = m_sidebarWrapper && m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper && m_filterWrapper->isVisible();
    bool targetVisible = !(sidebarVisible && filterVisible);

    if (m_sidebarWrapper) m_sidebarWrapper->setVisible(targetVisible);
    if (m_filterWrapper) m_filterWrapper->setVisible(targetVisible);

    
    QPushButton* btnSidebar = findChild<QPushButton*>("btnSidebar");
    if (btnSidebar) btnSidebar->setChecked(targetVisible);
    QPushButton* btnFilter = findChild<QPushButton*>("btnFilter");
    if (btnFilter) btnFilter->setChecked(targetVisible);

    updateLayoutWidth();
    updateToggleAllIcon();

    if (targetVisible && m_filterPanel) {
        m_filterPanel->updateStats(m_searchEdit->text(), m_currentFilterType, m_currentFilterValue);
    }
}

void QuickWindow::updateToggleAllIcon() {

    if (!m_btnToggleAll) return;

    bool sidebarVisible = m_sidebarWrapper && m_sidebarWrapper->isVisible();
    bool filterVisible = m_filterWrapper && m_filterWrapper->isVisible();

    bool bothVisible = (sidebarVisible && filterVisible);
    bool anyVisible = (sidebarVisible || filterVisible);

    

    QString iconName = !anyVisible ? "sidebar_open_filled" : "panel_right_filled";
    QString iconColor = "#3A90FF";
    m_btnToggleAll->setIcon(IconHelper::getIcon(iconName, iconColor));

    
    m_btnToggleAll->setChecked(bothVisible);
}

void QuickWindow::updateLayoutWidth() {

    bool sideVisible = m_sidebarWrapper && !m_sidebarWrapper->isHidden();
    bool filterVisible = m_filterWrapper && !m_filterWrapper->isHidden();
    int activeCount = (sideVisible ? 1 : 0) + (filterVisible ? 1 : 0);

    int minRequiredWidth = (activeCount == 2) ? 563 : 400;
    int minRequiredHeight = 700;

    
    this->setMinimumWidth(minRequiredWidth);
    this->setMinimumHeight(minRequiredHeight);

    if (this->width() < minRequiredWidth || this->height() < minRequiredHeight) {
        int targetW = qMax(this->width(), minRequiredWidth);
        int targetH = qMax(this->height(), minRequiredHeight);
        this->resize(targetW, targetH);
    }

    
    
    int currentWidth = this->width();
    int totalContentW = currentWidth - 80; 

    int filterSize = 0;
    int sideSize = 0;
    int handleW = 0;

    if (filterVisible) {
        filterSize = m_filterWidth;
        handleW += 4;
    }
    if (sideVisible) {
        sideSize = m_sidebarWidth;
        handleW += 4;
    }

    int listSize = totalContentW - filterSize - sideSize - handleW;
    
    if (listSize < 100) listSize = 100;

    
    m_splitter->setSizes({listSize, filterSize, sideSize});
}

void QuickWindow::showListContextMenu(const QPoint& pos) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        QModelIndex index = m_listView->indexAt(pos);
        if (index.isValid()) {
            m_listView->setCurrentIndex(index);
            selected << index;
        }
    }

    int selCount = selected.size();
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }");

    auto getHint = [](const QString& id) {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        return seq.isEmpty() ? "" : " (" + seq.toString(QKeySequence::NativeText).replace("+", " + ") + ")";
    };

    
    if (selCount == 0) {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), " 新建数据" + getHint("qw_new_idea"), this, &QuickWindow::doNewIdea);
        menu.exec(m_listView->mapToGlobal(pos));
        return;
    }

    if (selCount == 1) {

        menu.addAction(IconHelper::getIcon("eye", "#41F2F2", 18), "预览" + getHint("qw_preview"), this, &QuickWindow::doPreview);

        QString content = selected.first().data(NoteModel::ContentRole).toString();
        QString type = selected.first().data(NoteModel::TypeRole).toString();

        
        QString firstUrl = StringUtils::extractFirstUrl(content);
        if (!firstUrl.isEmpty()) {
            menu.addAction(IconHelper::getIcon("link", "#17B345", 18), "打开链接", [firstUrl]() {
                QDesktopServices::openUrl(QUrl(firstUrl));
            });
        }

        
        
        bool isPath = (type == "file" || type == "local_file" || type == "local_folder" || type == "local_batch");
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        QString path = content;

        if (!isPath) {
            
            if (QFileInfo(plainContent).exists() && QFileInfo(plainContent).isAbsolute()) {
                isPath = true;
                path = plainContent;
            }
        }

        if (isPath) {
            if (path.startsWith("attachments/")) {
                path = QCoreApplication::applicationDirPath() + "/" + path;
            }

            
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
    }

    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), QString("复制 (%1)").arg(selCount), this, &QuickWindow::doExtractContent);
    if (selCount >= 2) {

        menu.addAction(IconHelper::getIcon("merge", "#3498db", 18), QString("合并数据 (%1)").arg(selCount), this, &QuickWindow::doMergeSelected);
    }
    menu.addSeparator();

    if (selCount == 1) {
        menu.addAction(IconHelper::getIcon("edit", "#4a90e2", 18), "编辑" + getHint("qw_edit"), this, &QuickWindow::doEditSelected);

        QString tags = selected.first().data(NoteModel::TagsRole).toString();
        if (!tags.trimmed().isEmpty()) {
            menu.addAction(IconHelper::getIcon("copy_tags", "#9b59b6", 18), "复制标签" + getHint("qw_copy_tags"), this, &QuickWindow::doCopyTags);
        }

        if (!DatabaseManager::getTagClipboard().isEmpty()) {
            menu.addAction(IconHelper::getIcon("paste_tags", "#e67e22", 18), "粘贴标签" + getHint("qw_paste_tags"), this, &QuickWindow::doPasteTags);
        }

        menu.addSeparator();
    }

    auto* ratingMenu = menu.addMenu(IconHelper::getIcon("star_filled", "#f39c12", 18), QString("标记星级 (%1)").arg(selCount));
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

    bool isFavorite = selected.first().data(NoteModel::FavoriteRole).toBool();

    menu.addAction(IconHelper::getIcon(isFavorite ? "bookmark_filled" : "bookmark", isFavorite ? "#2ECC71" : "#aaaaaa", 18),
                   isFavorite ? "取消收藏" : "添加收藏" + getHint("qw_favorite"), this, &QuickWindow::doToggleFavorite);

    bool isPinned = selected.first().data(NoteModel::PinnedRole).toBool();

    menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18),
                   isPinned ? "取消置顶" : "置顶选中项" + getHint("qw_pin"), this, &QuickWindow::doTogglePin);

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
            if (!noteIds.isEmpty()) DatabaseManager::instance().updateNoteStateBatch(noteIds, "is_deleted", 0);
            if (!catIds.isEmpty()) DatabaseManager::instance().restoreCategories(catIds);
            refreshData();
            refreshSidebar();
            QString successMsg = selected.size() > 1 ? QString("[OK] 已恢复选中的 %1 个项目").arg(selected.size()) : "[OK] 已恢复 1 个项目";
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>" + successMsg + "</b>");
        });

        menu.addAction(IconHelper::getIcon("refresh", "#3498db", 18), "全部恢复 (还原所有)", [this](){
            if (DatabaseManager::instance().restoreAllFromTrash()) {
                refreshData();
                refreshSidebar();
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已将回收站内容全量还原</b>");
            }
        });

        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "彻底删除 (不可逆)", [this](){ doDeleteSelected(true); });
    } else {
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "删除" + getHint("qw_delete_soft"), [this](){ doDeleteSelected(false); });
    }

    if (m_currentFilterType != "recently_visited") {
        menu.addSeparator();
        auto* sortMenu = menu.addMenu(IconHelper::getIcon("list_ol", "#aaaaaa", 18), "排列");
        sortMenu->setStyleSheet(menu.styleSheet());

        sortMenu->addAction("上移" + getHint("qw_move_up"), [this](){ doMoveNote(DatabaseManager::Up); });
        sortMenu->addAction("下移" + getHint("qw_move_down"), [this](){ doMoveNote(DatabaseManager::Down); });
        sortMenu->addAction("移至顶部", [this](){ doMoveNote(DatabaseManager::Top); });
        sortMenu->addAction("移至底部", [this](){ doMoveNote(DatabaseManager::Bottom); });
        sortMenu->addSeparator();
        sortMenu->addAction("按标题 A→Z 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, true);
        });
        sortMenu->addAction("按标题 Z→A 排列", [this](){
            DatabaseManager::instance().reorderNotes(m_currentFilterType, m_currentFilterValue, false);
        });

        if (m_currentFilterType == "category" && m_currentFilterValue.toInt() > 0) {
            sortMenu->addSeparator();
            int catId = m_currentFilterValue.toInt();
            QString currentMode = DatabaseManager::instance().getCategorySortMode(catId);

            auto* manualAction = sortMenu->addAction("手动排序模式", [this, catId]() {
                DatabaseManager::instance().setCategorySortMode(catId, "manual");
                refreshData();
            });
            manualAction->setCheckable(true);
            manualAction->setChecked(currentMode != "created_at_desc");

            auto* createdAction = sortMenu->addAction("按创建日期排列", [this, catId]() {
                DatabaseManager::instance().setCategorySortMode(catId, "created_at_desc");
                refreshData();
            });
            createdAction->setCheckable(true);
            createdAction->setChecked(currentMode == "created_at_desc");
        }
    }

    menu.exec(m_listView->mapToGlobal(pos));
}

void QuickWindow::showSidebarMenu(const QPoint& pos) {
    auto* tree = qobject_cast<QTreeView*>(sender());
    if (!tree) return;

    QModelIndexList selected = tree->selectionModel()->selectedIndexes();
    QModelIndex index = tree->indexAt(pos);

    if (index.isValid() && !selected.contains(index)) {
        tree->setCurrentIndex(index); 
        selected.clear();
        selected << index;
    }

    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       
                       "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                       "QMenu::icon { margin-left: 6px; } "
                       "QMenu::item:selected { background-color: #3E3E42; color: white; }");

    QString type = index.data(CategoryModel::TypeRole).toString();
    QString idxName = index.data(CategoryModel::NameRole).toString();

    static const QStringList silentTypes = {"recently_visited", "untagged"};
    if (silentTypes.contains(type)) {
        return;
    }

    // [镜像模式适配]：如果在“快速访问”镜像组中点击，仅显示移除选项
    QModelIndex parentIdx = index.parent();
    if (parentIdx.isValid() && parentIdx.data(CategoryModel::NameRole).toString() == "快速访问") {
        int catId = index.data(CategoryModel::IdRole).toInt();
        menu.addAction(IconHelper::getIcon("pin_vertical", "#f1c40f", 18), "从“快速访问”移除", [this, catId]() {
            DatabaseManager::instance().toggleCategoryPinned(catId);
            refreshSidebar();
        });
        menu.exec(tree->mapToGlobal(pos));
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

    
    if (!index.isValid() || idxName == "我的分类") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
            FramelessInputDialog dlg("新建分类", "组名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
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

    if (type == "category") {
        int catId = index.data(CategoryModel::IdRole).toInt();
        QString currentName = index.data(CategoryModel::NameRole).toString();

        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this, catId]() {
            auto* win = new NoteEditWindow();
            win->setDefaultCategory(catId);
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
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

        importMenu->addAction(IconHelper::getIcon("package_rnp", "#9b59b6", 18), "导入专属安装包 (.rnp)", [this]() {
            FileStorageHelper::importFromPackage(this);
            this->refreshSidebar();
            this->refreshData();
        });

        auto* exportMenu = menu.addMenu(IconHelper::getIcon("file_export", "#3498db", 18), "导出");
        exportMenu->setStyleSheet(menu.styleSheet());

        QVariantMap rootCat = DatabaseManager::instance().getRootCategory(catId);
        QString rootName = rootCat.value("name").toString();
        int rootId = rootCat.value("id").toInt();

        if (rootId == catId) {

            exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                doExportCategory(rootId, rootName);
            });

            auto children = DatabaseManager::instance().getChildCategories(rootId);
            for (const auto& child : std::as_const(children)) {
                int childId = child.value("id").toInt();
                QString childName = child.value("name").toString();
                exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), childName, [this, childId, childName]() {
                    doExportCategory(childId, childName);
                });
            }
        } else {

            exportMenu->addAction(IconHelper::getIcon("branch", "#3498db", 18), currentName, [this, catId, currentName]() {
                doExportCategory(catId, currentName);
            });
            if (!rootName.isEmpty()) {
                exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), rootName, [this, rootId, rootName]() {
                    doExportCategory(rootId, rootName);
                });
            }
        }

        exportMenu->addSeparator();
        exportMenu->addAction(IconHelper::getIcon("folder", "#3498db", 18), "整分类", [this, rootId, rootName]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportCategoryRecursive(rootId, rootName, this);
        });

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
                    refreshSidebar();
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
            refreshSidebar();
        });
        menu.addAction(IconHelper::getIcon("tag", "#FFAB91", 18), "设置预设标签", [this, catId]() {
            QString currentTags = DatabaseManager::instance().getCategoryPresetTags(catId);
            FramelessInputDialog dlg("设置预设标签", "标签 (逗号分隔):", currentTags, this);
            if (dlg.exec() == QDialog::Accepted) {
                DatabaseManager::instance().setCategoryPresetTags(catId, dlg.text());
            }
        });

        // 2026-05-16 按照用户要求：补全“归类到此分类”菜单项，用于设定自动归档的目标分类
        menu.addAction(IconHelper::getIcon("switch_on", "#00A650", 18), "归类到此分类", [this, catId, currentName]() {
            // 核心修复：设定目标的同时，物理开启自动归档开关，确保图标变绿显示
            DatabaseManager::instance().setAutoCategorizeEnabled(true);
            DatabaseManager::instance().setActiveCategoryId(catId);
            updateAutoCategorizeButton();
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #00A650;'>[OK] 已将“%1”设为自动归档目标并开启开关</b>").arg(currentName));
        });

        menu.addSeparator();

        
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建分类", [this]() {
            FramelessInputDialog dlg("新建分类", "组名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text);
                    refreshSidebar();
                }
            }
        });
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建子分类", [this, catId]() {
            FramelessInputDialog dlg("新建子分类", "区名称:", "", this);
            if (dlg.exec() == QDialog::Accepted) {
                QString text = dlg.text();
                if (!text.isEmpty()) {
                    DatabaseManager::instance().addCategory(text, catId);
                    refreshSidebar();
                }
            }
        });
        menu.addSeparator();

        if (selected.size() == 1) {
            bool isPinned = index.data(CategoryModel::PinnedRole).toBool();

            menu.addAction(IconHelper::getIcon(isPinned ? "pin_vertical" : "pin_tilted", isPinned ? "#FF551C" : "#aaaaaa", 18),
                           isPinned ? "从“快速访问”移除" : "添加至“快速访问”", [this, catId]() {
                DatabaseManager::instance().toggleCategoryPinned(catId);
                refreshSidebar();
            });

            menu.addAction(IconHelper::getIcon("edit", "#aaaaaa", 18), "重命名", [this, index]() {
                m_partitionTree->edit(index);
            });
        }

        QString delText = selected.size() > 1 ? QString("删除选中的 %1 个分类").arg(selected.size()) : "删除";
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), delText, [this, selected]() {

            QString msg = selected.size() > 1 ? "确定要删除选中的分类吗？\n(分类将永久消失，内容将删除)" : "确定要删除此分类吗？\n(分类将永久消失，内容将删除)";
            FramelessMessageBox dlg("确认删除", msg, this);
            if (dlg.exec() == QDialog::Accepted) {
                QList<int> ids;
                for (const auto& idx : selected) {
                    if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                        ids << idx.data(CategoryModel::IdRole).toInt();
                    }
                }
                qDebug() << "[QuickWindow] 准备物理删除分类，提取到的 IDs:" << ids;
                DatabaseManager::instance().hardDeleteCategories(ids);
                refreshSidebar();
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
                    refreshSidebar();
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
                            refreshSidebar();
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
                        refreshSidebar();
                        refreshData();
                    } else {
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密码错误</b>");
                    }
                }
            });
        });
        pwdMenu->addAction("立即锁定", [this, catId]() {
            DatabaseManager::instance().lockCategory(catId);
            refreshSidebar();
            refreshData();
        })->setShortcut(QKeySequence("Ctrl+S"));
    } else if (idxName == "未分类" || type == "uncategorized") {
        menu.addAction(IconHelper::getIcon("add", "#3498db", 18), "新建数据", [this]() {
            auto* win = new NoteEditWindow();
            connect(win, &NoteEditWindow::noteSaved, this, &QuickWindow::refreshData);
            win->show();
        });
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("file_export", "#3498db", 18), "导出 [未分类]", [this]() {
            if (!verifyExportPermission()) return;
            FileStorageHelper::exportByFilter("uncategorized", -1, "未分类灵感", this);
        });
    }

    if (idxName == "回收站" || type == "trash") {
        menu.addAction(IconHelper::getIcon("refresh", "#2ecc71", 18), "全部恢复（未分类）", this, &QuickWindow::doRestoreTrash);
        menu.addSeparator();
        menu.addAction(IconHelper::getIcon("trash", "#e74c3c", 18), "清空回收站", [this]() {
            FramelessMessageBox dlg("确认清空", "确定要永久删除回收站中的所有内容吗？\n(此操作不可逆)", this);
            if (dlg.exec() == QDialog::Accepted) {
                DatabaseManager::instance().emptyTrash();
                refreshData();
                refreshSidebar();
            }
        });
    }

    menu.exec(tree->mapToGlobal(pos));
}

void QuickWindow::doMoveToCategory(int catId) {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    if (catId != -1) {
        StringUtils::recordRecentCategory(catId);
    }

    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();

    DatabaseManager::instance().moveNotesToCategory(ids, catId);

    ActionRecorder::instance().recordMoveToCategory(catId);
    refreshData();
}

void QuickWindow::doMoveNote(DatabaseManager::MoveDirection dir) {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return;

    int id = index.data(NoteModel::IdRole).toInt();
    if (DatabaseManager::instance().moveNote(id, dir, m_currentFilterType, m_currentFilterValue)) {
        
    }
}

void QuickWindow::sendNote(const QVariantMap& note) {

    if (note.isEmpty()) return;

    int id = note.value("id").toInt();
    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();

    
    DatabaseManager::instance().recordAccess(id);

    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else {
        StringUtils::copyNoteToClipboard(content);
    }

#ifdef Q_OS_WIN
    if (m_lastActiveHwnd && IsWindow(m_lastActiveHwnd)) {
        if (IsIconic(m_lastActiveHwnd)) ShowWindow(m_lastActiveHwnd, SW_RESTORE);
        SetForegroundWindow(m_lastActiveHwnd);

        QTimer::singleShot(300, [this]() {
            INPUT inputs[4];
            memset(inputs, 0, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));
        });
    }
#endif

    
    updateContextSnapshotById(id);
}

bool QuickWindow::verifyExportPermission() {

    QSettings settings("RapidNotes", "QuickWindow");
    QString savedPwd = settings.value("appPassword").toString();

    
    if (savedPwd.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(),
            "<b style='color: #e74c3c;'>[安全拦截] 您尚未设置应用密码，请先前往主程序“设置-安全”中设置导出授权密码。</b>", 5000);
        return false;
    }

    PasswordVerifyDialog dlg("导出身份验证", "请输入应用启动密码以授权本次导出操作", this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.password() == savedPwd) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 验证成功，正在准备导出...</b>", 1500);
            return true;
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密码错误，导出已取消。</b>", 3000);
            return false;
        }
    }
    return false;
}

void QuickWindow::updateContextSnapshotById(int noteId) {

    QList<QVariantMap> allNotes = DatabaseManager::instance().searchNotes("", m_currentFilterType, m_currentFilterValue);
    int centerIdx = -1;
    for (int i = 0; i < allNotes.size(); ++i) {
        if (allNotes[i]["id"].toInt() == noteId) {
            centerIdx = i;
            break;
        }
    }

    if (centerIdx != -1) {
        m_contextNotesSnapshot.clear();
        int start = qMax(0, centerIdx - 5);
        int end = qMin(allNotes.size() - 1, centerIdx + 5);
        for (int i = start; i <= end; ++i) {
            m_contextNotesSnapshot.append(allNotes[i]);
        }
    }
}

void QuickWindow::showContextNotesMenu() {
    // 2026-05-10 按照用户要求：彻底拔除旧版基于 QMenu 的上下文快照逻辑
    // 统一重定向至 RecentNotesWindow 简约悬浮列表，支持最近访问与收藏标签页切换
    RecentNotesWindow::instance()->popup(QCursor::pos());
}

void QuickWindow::checkIdleLock() {
#ifdef Q_OS_WIN

    QSettings settings("RapidNotes", "Security");
    if (!settings.value("idleLockEnabled", false).toBool()) return;

    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD idleTime = (GetTickCount() - lii.dwTime) / 1000;
        if (idleTime >= 30 && !isLocked()) {
            qDebug() << "[QuickWindow] 检测到系统闲置" << idleTime << "秒，触发自动锁定。";
            doGlobalLock();
        }
    }
#endif
}

void QuickWindow::handleTagInput() {
    QString text = m_tagEdit->text().trimmed();
    if (text.isEmpty()) return;

    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tags = { text };
    for (const auto& index : std::as_const(selected)) {
        int id = index.data(NoteModel::IdRole).toInt();
        DatabaseManager::instance().addTagsToNote(id, tags);
    }

    m_tagEdit->clear();
    refreshData();
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 标签已添加</b>");
}

void QuickWindow::openTagSelector() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList currentTags;
    if (selected.size() == 1) {
        int id = selected.first().data(NoteModel::IdRole).toInt();
        QVariantMap note = DatabaseManager::instance().getNoteById(id);
        currentTags = note.value("tags").toString().split(",", Qt::SkipEmptyParts);
    }

    for (QString& t : currentTags) t = t.trimmed();

    auto* selector = new AdvancedTagSelector(this);
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);

    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this, selected](const QStringList& tags){
        for (const auto& index : std::as_const(selected)) {
            int id = index.data(NoteModel::IdRole).toInt();
            DatabaseManager::instance().updateNoteState(id, "tags", tags.join(", "));
        }
        refreshData();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 标签已更新</b>");
    });

    selector->showAtCursor();
}

void QuickWindow::doCopyTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    
    int id = selected.first().data(NoteModel::IdRole).toInt();
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    QString tagsStr = note.value("tags").toString();
    QStringList tags = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : tags) t = t.trimmed();

    DatabaseManager::setTagClipboard(tags);

    // 2026-04-xx 提升为 ActivePriority 以防被覆盖
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已复制 %1 个标签</b>").arg(tags.size()), 700, QColor("#B0B0B0"), ToolTipOverlay::ActivePriority);
}

void QuickWindow::doPasteTags() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    QStringList tagsToPaste = DatabaseManager::getTagClipboard();
    if (tagsToPaste.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #aaaaaa;'>[!] 标签剪贴板为空</b>");
        return;
    }

    
    QList<int> ids;
    for (const auto& index : std::as_const(selected)) ids << index.data(NoteModel::IdRole).toInt();
    DatabaseManager::instance().updateNoteStateBatch(ids, "tags", tagsToPaste.join(", "));

    refreshData();

    ActionRecorder::instance().recordPasteTags(tagsToPaste);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已覆盖粘贴标签至 %1 条数据</b>").arg(selected.size()));
}

void QuickWindow::doRepeatAction() {
    auto selected = m_listView->selectionModel()->selectedIndexes();
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
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 已重复：%1 条数据分类归位</b>").arg(ids.size()));
    }
}

void QuickWindow::focusLockInput() {
    if (m_appLockWidget) {
        static_cast<AppLockWidget*>(m_appLockWidget)->focusInput();
    }
}

void QuickWindow::recordLastActiveWindow(HWND captureHwnd) {
#ifdef Q_OS_WIN
    
    
    

    HWND targetHwnd = captureHwnd;
    if (!targetHwnd) {
        targetHwnd = GetForegroundWindow();
    }

    HWND myHwnd = (HWND)winId();
    DWORD myPid = GetCurrentProcessId();

    auto isTaskbarOrDesktop = [](HWND hwnd) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        QString cls = QString::fromLatin1(className);
        return cls == "Shell_TrayWnd" || cls == "Progman" || cls == "WorkerW";
    };

    auto isInternalToolWindow = [](HWND hwnd) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        
        return QString::fromLatin1(className).contains("ToolSaveBits");
    };

    
    if (targetHwnd == nullptr || targetHwnd == myHwnd || isInternalToolWindow(targetHwnd)) {
        HWND next = GetWindow(GetForegroundWindow(), GW_HWNDNEXT);
        while (next) {
            DWORD pid = 0;
            GetWindowThreadProcessId(next, &pid);
            if (pid != myPid && IsWindowVisible(next) && !isTaskbarOrDesktop(next) && !isInternalToolWindow(next)) {
                targetHwnd = next;
                break;
            }
            next = GetWindow(next, GW_HWNDNEXT);
        }
    }

    
    DWORD targetPid = 0;
    GetWindowThreadProcessId(targetHwnd, &targetPid);
    if (targetHwnd && targetHwnd != myHwnd && targetPid != myPid && !isTaskbarOrDesktop(targetHwnd)) {
        m_lastActiveHwnd = targetHwnd;
        m_lastThreadId = GetWindowThreadProcessId(m_lastActiveHwnd, nullptr);

        GUITHREADINFO gti;
        gti.cbSize = sizeof(GUITHREADINFO);
        if (GetGUIThreadInfo(m_lastThreadId, &gti)) {
            m_lastFocusHwnd = gti.hwndFocus;
        } else {
            m_lastFocusHwnd = nullptr;
        }
    }
#endif
}

void QuickWindow::showAuto() {
#ifdef Q_OS_WIN
    HWND myHwnd = (HWND)winId();
    
    recordLastActiveWindow(nullptr);
#endif

    
    QSettings settings("RapidNotes", "QuickWindow");
    if (!settings.contains("geometry")) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            move(screenGeom.center() - rect().center());
        }
    }

    QPoint targetPos = pos();
    bool wasHidden = !isVisible() || isMinimized();

    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }

    if (wasHidden) {
        setWindowOpacity(0);
        auto* fade = new QPropertyAnimation(this, "windowOpacity");
        fade->setDuration(300);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);

        auto* slide = new QPropertyAnimation(this, "pos");
        slide->setDuration(300);
        slide->setStartValue(targetPos + QPoint(0, 10));
        slide->setEndValue(targetPos);
        slide->setEasingCurve(QEasingCurve::OutCubic);

        fade->start(QAbstractAnimation::DeleteWhenStopped);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
    }

    raise();
    activateWindow();

#ifdef Q_OS_WIN
    
    SetForegroundWindow(myHwnd);
#endif

    if (isLocked()) {
        focusLockInput();
    } else {

        m_listView->setFocus();
        if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
            m_listView->setCurrentIndex(m_model->index(0, 0));
        }
    }
}

void QuickWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    
    updateAppLockStatus();

    
    if (m_listView && !isLocked()) {
        m_listView->setFocus();
        if (m_model->rowCount() > 0 && !m_listView->currentIndex().isValid()) {
            m_listView->setCurrentIndex(m_model->index(0, 0));
        }
    }

#ifdef Q_OS_WIN
    HWND myHwnd = (HWND)winId();
    
    // [STABILITY] 2026-04-18 延迟注入原生窗口属性，确保 DWM 环境已就绪
    static bool nativeAttrsApplied = false;
    if (!nativeAttrsApplied && myHwnd) {
        // 1. Windows 11 原生圆角 (Attribute: 33, Value: 2)
        DWORD cornerPreference = 2; 
        DwmSetWindowAttribute(myHwnd, 33, &cornerPreference, sizeof(cornerPreference));
        
        // 2. 任务栏最小化支持样式
        StringUtils::applyTaskbarMinimizeStyle((void*)myHwnd);
        
        nativeAttrsApplied = true;
    }

    if (m_isStayOnTop) {
        SetWindowPos(myHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        
        SetWindowPos(myHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        QTimer::singleShot(150, [myHwnd]() {
            SetWindowPos(myHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        });
    }
#endif
}

#ifdef Q_OS_WIN
bool QuickWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);

    
    if (msg->message == WM_ERASEBKGND) {
        *result = 1;
        return true;
    }

    
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

        qintptr hit = HTNOWHERE;
        if (top && left) hit = HTTOPLEFT;
        else if (top && right) hit = HTTOPRIGHT;
        else if (bottom && left) hit = HTBOTTOMLEFT;
        else if (bottom && right) hit = HTBOTTOMRIGHT;
        else if (top) hit = HTTOP;
        else if (bottom) hit = HTBOTTOM;
        else if (left) hit = HTLEFT;
        else if (right) hit = HTRIGHT;

        if (hit != HTNOWHERE) {

            
            
            QWidget* child = childAt(pos);
            if (qobject_cast<QPushButton*>(child)) {
                return false;
            }
            *result = hit;
            return true;
        }

        return QWidget::nativeEvent(eventType, message, result);
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

bool QuickWindow::event(QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        
        HotkeyManager::instance().unregisterHotkey(4);
        qDebug() << "[QuickWindow] 窗口激活，已物理注销全局 Ctrl+S 采集热键。";
    } else if (event->type() == QEvent::WindowStateChange) {
        if (this->isMaximized()) {
            if (m_mainLayout) m_mainLayout->setContentsMargins(0, 0, 0, 0);
            if (m_btnMax) {
                m_btnMax->setIcon(IconHelper::getIcon("restore_window", "#aaaaaa"));
                m_btnMax->setProperty("tooltipText", "还原");
            }
        } else {
            if (m_mainLayout) m_mainLayout->setContentsMargins(12, 12, 12, 12);
            if (m_btnMax) {
                m_btnMax->setIcon(IconHelper::getIcon("maximize", "#aaaaaa"));
                m_btnMax->setProperty("tooltipText", "最大化");
            }
        }
    }
    return QWidget::event(event);
}

void QuickWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        
        if (auto* handle = windowHandle()) {
            handle->startSystemMove();
        }
        event->accept();
    }
}

void QuickWindow::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
}

void QuickWindow::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event);
}

void QuickWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void QuickWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void QuickWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();

    
    
    if (mime->hasFormat("application/x-note-ids")) {
        event->ignore();
        return;
    }

    int targetId = -1;
    if (m_currentFilterType == "category") {
        targetId = m_currentFilterValue.toInt();
    }

    
    
    QPoint globalDropPos = mapToGlobal(event->position().toPoint());

    auto checkTree = [&](QTreeView* tree) {
        if (tree && tree->isVisible()) {
            
            QPoint viewportPos = tree->viewport()->mapFromGlobal(globalDropPos);
            if (tree->viewport()->rect().contains(viewportPos)) {
                QModelIndex idx = tree->indexAt(viewportPos);
                if (idx.isValid() && idx.data(CategoryModel::TypeRole).toString() == "category") {
                    targetId = idx.data(CategoryModel::IdRole).toInt();
                    return true;
                }
            }
        }
        return false;
    };

    if (!checkTree(m_systemTree)) {
        checkTree(m_partitionTree);
    }

    QString itemType = "text";
    QString title;
    QString content;
    QByteArray dataBlob;
    QStringList tags;

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
            content = remoteUrls.join(";");
            title = "外部链接";
            itemType = "link";
        }
    } else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        content = mime->text();
        title = content.trimmed().left(50).replace("\n", " ");
        itemType = "text";
    } else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            itemType = "image";
            title = "[拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            content = "[Image Data]";
        }
    }

    if (!content.isEmpty() || !dataBlob.isEmpty()) {
        DatabaseManager::instance().addNote(title, content, tags, "", targetId, itemType, dataBlob);
        event->acceptProposedAction();
    }
}

void QuickWindow::hideEvent(QHideEvent* event) {
    
    
    if (m_appLockWidget && !event->spontaneous() && !isVisible()) {
        qDebug() << "[QuickWin] 退出程序，因为应用锁处于活动状态且窗口被隐藏";
        QApplication::quit();
    }
    saveState();
    QWidget::hideEvent(event);
}

void QuickWindow::resizeEvent(QResizeEvent* event) {
    if (m_appLockWidget) {
        m_appLockWidget->resize(this->size());
    }
    QWidget::resizeEvent(event);
    saveState();
}

void QuickWindow::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    saveState();
}

void QuickWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // 2026-04-xx 按照用户要求：Esc 键优先清空文字，文字为空时才关闭窗口
        bool searchHasText = m_searchEdit && !m_searchEdit->text().isEmpty();
        bool tagHasText = m_tagEdit && !m_tagEdit->text().isEmpty();
        bool catSearchHasText = m_catSearchEdit && !m_catSearchEdit->text().isEmpty();

        if (searchHasText || tagHasText || catSearchHasText) {
            if (searchHasText) m_searchEdit->clear();
            if (tagHasText) m_tagEdit->clear();
            if (catSearchHasText) m_catSearchEdit->clear();
            event->accept();
            return;
        }

        hide();
        return;
    }
    QWidget::keyPressEvent(event);
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

void QuickWindow::doExportCategory(int catId, const QString& catName) {
    if (!verifyExportPermission()) return;
    FileStorageHelper::exportCategory(catId, catName, this);
}

void QuickWindow::doImportCategory(int catId) {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择导入文件", "", "所有文件 (*.*);;CSV文件 (*.csv)");
    if (files.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(files, catId);

    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 导入完成，共处理 %1 个项目</b>").arg(totalCount));
}

void QuickWindow::doImportFolder(int catId) {

    QFileDialog dialog(this);
    dialog.setWindowTitle("选择导入文件夹 (可多选)");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true); 

    
    QListView *listView = dialog.findChild<QListView*>("listView");
    if (listView) listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QTreeView *treeView = dialog.findChild<QTreeView*>("treeView");
    if (treeView) treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (dialog.exec() != QDialog::Accepted) return;
    QStringList dirs = dialog.selectedFiles();
    if (dirs.isEmpty()) return;

    int totalCount = FileStorageHelper::processImport(dirs, catId);

    refreshData();
    refreshSidebar();
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 批量导入完成，共处理 %1 个目录共 %2 个项目</b>").arg(dirs.size()).arg(totalCount));
}

void QuickWindow::updateFocusLines() {
    QWidget* focus = QApplication::focusWidget();

    
    bool sidebarVisible = m_systemTree->parentWidget() && m_systemTree->parentWidget()->isVisible() && m_systemTree->parentWidget()->width() > 10;

    bool listFocus = (focus == m_listView) && sidebarVisible;
    bool sidebarFocus = (focus == m_systemTree || focus == m_partitionTree) && sidebarVisible;

    if (m_listFocusLine) m_listFocusLine->setVisible(listFocus);
    if (m_sidebarFocusLine) m_sidebarFocusLine->setVisible(sidebarFocus);
}

bool QuickWindow::eventFilter(QObject* watched, QEvent* event) {
    
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            if (keyEvent->modifiers() & Qt::AltModifier || !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                event->accept();
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        
        qDebug() << "KeyPress:" << QKeySequence(keyEvent->key()).toString()
                 << "Mods:" << keyEvent->modifiers()
                 << "FocusWidget:" << (watched ? watched->objectName() : "None");

        if (keyEvent->key() == Qt::Key_E && (keyEvent->modifiers() & Qt::AltModifier)) {
            toggleFilter();
            return true;
        }

        // 2026-04-xx 修正逻辑：必须严格匹配 Ctrl+G，严禁在按下 Ctrl+Shift+G 时误触。
        if (keyEvent->key() == Qt::Key_G && keyEvent->modifiers() == Qt::ControlModifier) {
            if (m_filterWrapper && m_filterWrapper->isVisible() && m_filterPanel) {
                m_filterPanel->toggleAllGroups();
            }
            return true;
        }

        // 2026-04-xx 增加对重置筛选快捷键 Ctrl+Shift+G 的全局响应支持
        if (keyEvent->key() == Qt::Key_G && keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (m_filterWrapper && m_filterWrapper->isVisible() && m_filterPanel) {
                m_filterPanel->resetFilters();
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_R && (keyEvent->modifiers() & Qt::AltModifier)) {
            toggleAllPanels();
            return true;
        }

        
        if (keyEvent->key() == Qt::Key_S && (keyEvent->modifiers() & Qt::ControlModifier)) {
            auto mods = keyEvent->modifiers();

            
            if (mods & Qt::AltModifier) {
                qDebug() << "[QuickWindow] 物理拦截捕获到 Ctrl+Alt+S, 切换显示/隐藏。";
                auto& db = DatabaseManager::instance();
                db.toggleLockedCategoriesVisibility();
                bool isHidden = db.isLockedCategoriesHidden();

                
                if (isHidden && m_currentFilterType == "category" && m_currentFilterValue != -1) {
                    int catId = m_currentFilterValue.toInt();
                    if (db.isCategoryLocked(catId) || !db.getCategoryPresetTags(catId).isNull()) {
                         m_currentFilterType = "all";
                         m_currentFilterValue = -1;
                    }
                }

                refreshSidebar();
                refreshData();
                ToolTipOverlay::instance()->showText(QCursor::pos(), isHidden ?
                    "<b style='color: #e67e22;'>[OK] 已隐藏加锁分类并强制重锁</b>" :
                    "<b style='color: #2ecc71;'>[OK] 已显示所有分类并强制重锁</b>");
                return true;
            }

            
            if (!(mods & Qt::ShiftModifier)) {
                qDebug() << "[QuickWindow] 物理拦截捕获到 Ctrl+S, 准备执行上锁。";
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
                    refreshSidebar();
                    refreshData();
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f39c12;'>[OK] 分类已物理锁定</b>");
                    return true;
                }
            }
        }
    }

    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {

            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true;
    }

    
    
    if (watched == this && event->type() == QEvent::WindowActivate) {
        recordLastActiveWindow(nullptr);
    }


    if (watched == m_listView && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        int delta = wheelEvent->angleDelta().y();
        QScrollBar* vb = m_listView->verticalScrollBar();

        
        if (!m_lastWheelPageTimer.isValid() || m_lastWheelPageTimer.elapsed() > 600) {
            
            if (delta < 0 && vb->value() >= vb->maximum()) {
                if (m_currentPage < m_totalPages) {
                    m_currentPage++;
                    refreshData();
                    m_lastWheelPageTimer.start();

                    ToolTipOverlay::instance()->showText(QCursor::pos(),
                        QString("<b style='color: #2ecc71;'>[下一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                    return true;
                }
            }
            
            else if (delta > 0 && vb->value() <= vb->minimum()) {
                if (m_currentPage > 1) {
                    m_currentPage--;
                    refreshData();
                    m_lastWheelPageTimer.start();

                    ToolTipOverlay::instance()->showText(QCursor::pos(),
                        QString("<b style='color: #3498db;'>[上一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                    
                    QTimer::singleShot(10, [vb](){ vb->setValue(vb->maximum()); });
                    return true;
                }
            }
        }
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateFocusLines();

        
        
        if (event->type() == QEvent::FocusIn && !m_isSidebarPersistent) {
            if (watched == m_searchEdit || watched == m_listView) {
                if (m_sidebarWrapper && m_sidebarWrapper->isVisible()) {
                    toggleSidebar();
                }
            }
        }
    }

    if (watched->objectName() == "btnSidebar") {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                QMenu menu(this);
                menu.setStyleSheet(R"(
                    QMenu { background-color: #252526; border: 1px solid #454545; color: #cccccc; padding: 4px; }
                    QMenu::item { padding: 4px 20px; border-radius: 4px; }
                    QMenu::item:selected { background-color: #094771; color: white; }
                    QMenu::item:checked { color: #2ecc71; font-weight: bold; }
                )");

                QAction* autoFold = menu.addAction("自动收起");
                autoFold->setCheckable(true);
                autoFold->setChecked(!m_isSidebarPersistent);

                QAction* manualFold = menu.addAction("人工收起");
                manualFold->setCheckable(true);
                manualFold->setChecked(m_isSidebarPersistent);

                connect(autoFold, &QAction::triggered, this, [this](){
                    m_isSidebarPersistent = false;
                    QSettings settings("RapidNotes", "QuickWindow");
                    settings.setValue("sidebarPersistent", false);

                    QWidget* focus = QApplication::focusWidget();
                    if ((focus == m_searchEdit || focus == m_listView) && m_sidebarWrapper->isVisible()) {
                        toggleSidebar();
                    }

                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[OK] 已切换为自动收起模式</b>", 1500);
                });

                connect(manualFold, &QAction::triggered, this, [this](){
                    m_isSidebarPersistent = true;
                    QSettings settings("RapidNotes", "QuickWindow");
                    settings.setValue("sidebarPersistent", true);
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已切换为人工收起模式</b>", 1500);
                });

                menu.exec(mouseEvent->globalPosition().toPoint());
                return true;
            }
        }
        
        if (event->type() == QEvent::MouseButtonDblClick) return true;
    }

    

    QWidget* customToolbar = findChild<QWidget*>("customToolbar");
    if (watched == m_listView || watched == m_systemTree || watched == m_partitionTree || watched == customToolbar) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter) {
            setCursor(Qt::ArrowCursor);
            
            if (watched == customToolbar) update();
        }
    }

    
    if ((watched == m_partitionTree || watched == m_systemTree) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();
        auto modifiers = keyEvent->modifiers();

        
        if (key == Qt::Key_QuoteLeft || key == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            m_currentCategoryColor = "#4a90e2";
            updatePartitionStatus("");
            applyListTheme("");
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        if (key == Qt::Key_F2) {
            if (watched == m_partitionTree) {
                QModelIndex current = m_partitionTree->currentIndex();
                if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                    
                    m_partitionTree->edit(current);
                }
            }
            return true;
        }

        if (key == Qt::Key_Escape) {
            // 2026-04-xx 按照用户要求：Esc 全局清空优先逻辑
            bool searchHasText = m_searchEdit && !m_searchEdit->text().isEmpty();
            bool tagHasText = m_tagEdit && !m_tagEdit->text().isEmpty();
            bool catSearchHasText = m_catSearchEdit && !m_catSearchEdit->text().isEmpty();

            if (searchHasText || tagHasText || catSearchHasText) {
                if (searchHasText) m_searchEdit->clear();
                if (tagHasText) m_tagEdit->clear();
                if (catSearchHasText) m_catSearchEdit->clear();
                return true;
            } else {
                m_listView->setFocus();
                return true;
            }
        }

        
        
        if (key == Qt::Key_Tab && (watched == m_partitionTree || watched == m_systemTree) &&
            m_systemTree->parentWidget()->isVisible()) {
            
            m_listView->setFocus();
            auto* model = m_listView->model();
            if (model && !m_listView->currentIndex().isValid() && model->rowCount() > 0) {
                m_listView->setCurrentIndex(model->index(0, 0));
            }
            return true;
        }

        if (key == Qt::Key_Delete) {
            if (watched == m_partitionTree) {
                auto selected = m_partitionTree->selectionModel()->selectedIndexes();
                if (!selected.isEmpty()) {
                    QString msg = selected.size() > 1 ? QString("确定要删除选中的 %1 个分类及其下所有内容吗？").arg(selected.size()) : "确定要删除选中的分类及其下所有内容吗？";
                    FramelessMessageBox dlg("确认删除", msg, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        QList<int> ids;
                        for (const auto& idx : selected) {
                            if (idx.data(CategoryModel::TypeRole).toString() == "category") {
                                ids << idx.data(CategoryModel::IdRole).toInt();
                            }
                        }
                        DatabaseManager::instance().softDeleteCategories(ids);
                        refreshSidebar();
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
                            refreshSidebar();
                        }
                    }
                }
            }
            return true;
        }

        
        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            QModelIndex current = (watched == m_partitionTree) ? m_partitionTree->currentIndex() : m_systemTree->currentIndex();
            if (current.isValid() && current.data(CategoryModel::TypeRole).toString() == "category") {
                int catId = current.data(CategoryModel::IdRole).toInt();
                DatabaseManager::MoveDirection dir;

                if (key == Qt::Key_Up) {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Top : DatabaseManager::Up;
                } else {
                    dir = (modifiers & Qt::ShiftModifier) ? DatabaseManager::Bottom : DatabaseManager::Down;
                }

                if (DatabaseManager::instance().moveCategory(catId, dir)) {
                    refreshSidebar();
                    return true;
                }
            }
        }
    }

    if (watched == m_systemTree || watched == m_partitionTree) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                QTreeView* tree = qobject_cast<QTreeView*>(watched);
                if (tree && tree->indexAt(me->pos()).isValid()) {
                    setCursor(Qt::PointingHandCursor);
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            setCursor(Qt::ArrowCursor);
        }
    }

    if (watched == m_listView && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        auto modifiers = keyEvent->modifiers();
        int key = keyEvent->key();

        
        if (key == Qt::Key_C && (modifiers & Qt::ControlModifier)) {
            doExtractContent();
            return true; 
        }

        
        if (keyEvent->key() == Qt::Key_QuoteLeft || keyEvent->key() == Qt::Key_Backspace) {
            m_currentFilterType = "all";
            m_currentFilterValue = -1;
            m_currentPage = 1;

            m_systemTree->selectionModel()->clearSelection();
            m_systemTree->setCurrentIndex(QModelIndex());
            m_partitionTree->selectionModel()->clearSelection();
            m_partitionTree->setCurrentIndex(QModelIndex());

            m_currentCategoryColor = "#4a90e2";
            updatePartitionStatus("");
            applyListTheme("");
            refreshData();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已切换至全部数据");
            return true;
        }

        
        if ((key == Qt::Key_Up || key == Qt::Key_Down) && (modifiers & Qt::AltModifier)) {
            DatabaseManager::MoveDirection dir;
            if (modifiers & Qt::ShiftModifier) dir = (key == Qt::Key_Up) ? DatabaseManager::Top : DatabaseManager::Bottom;
            else if (modifiers & Qt::ControlModifier) dir = (key == Qt::Key_Up) ? DatabaseManager::Top : DatabaseManager::Bottom;
            else dir = (key == Qt::Key_Up) ? DatabaseManager::Up : DatabaseManager::Down;

            doMoveNote(dir);
            return true;
        }
    }

    if ((watched == m_listView || watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        
        if (watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) {
            if (keyEvent->key() == Qt::Key_Up) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit) {
                    edit->setCursorPosition(0);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit) {
                    edit->setCursorPosition(edit->text().length());
                    return true;
                }
            }
        }

        if (keyEvent->key() == Qt::Key_F2 && watched == m_listView) {
            QModelIndex current = m_listView->currentIndex();
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

        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (watched == m_listView) {
                activateNote(m_listView->currentIndex());
                return true;
            }
        }

        
        
        if (keyEvent->key() == Qt::Key_Tab && watched == m_listView && m_systemTree->parentWidget()->isVisible()) {
            
            if (m_partitionTree->isVisible()) {
                m_partitionTree->setFocus();
                if (!m_partitionTree->currentIndex().isValid()) {
                    m_partitionTree->setCurrentIndex(m_partitionProxyModel->index(0, 0));
                }
            } else if (m_systemTree->isVisible()) {
                m_systemTree->setFocus();
            }
            return true;
        }

        if (watched == m_listView && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
            if (keyEvent->key() == Qt::Key_1) {
                if (m_model->rowCount() > 0) {
                    QModelIndex current = m_listView->currentIndex();
                    
                    if (current.isValid() && current.row() == 0) {
                        if (m_currentPage > 1) {
                            m_currentPage--;
                        } else {
                            m_currentPage = m_totalPages; 
                        }
                        refreshData();
                        ToolTipOverlay::instance()->showText(QCursor::pos(),
                            QString("<b style='color: #3498db;'>[上一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                        
                        if (m_model->rowCount() > 0) {
                            QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                            m_listView->setCurrentIndex(lastIdx);
                            m_listView->scrollTo(lastIdx);
                        }
                    } else {
                        
                        QModelIndex firstIdx = m_model->index(0, 0);
                        m_listView->setCurrentIndex(firstIdx);
                        m_listView->scrollTo(firstIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_2) {
                if (m_model->rowCount() > 0) {
                    QModelIndex current = m_listView->currentIndex();
                    
                    if (current.isValid() && current.row() == m_model->rowCount() - 1) {
                        if (m_currentPage < m_totalPages) {
                            m_currentPage++;
                        } else {
                            m_currentPage = 1; 
                        }
                        refreshData();
                        ToolTipOverlay::instance()->showText(QCursor::pos(),
                            QString("<b style='color: #2ecc71;'>[下一页] 第 %1 / %2 页</b>").arg(m_currentPage).arg(m_totalPages), 2000);
                        
                        if (m_model->rowCount() > 0) {
                            QModelIndex firstIdx = m_model->index(0, 0);
                            m_listView->setCurrentIndex(firstIdx);
                            m_listView->scrollTo(firstIdx);
                        }
                    } else {
                        
                        QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                        m_listView->setCurrentIndex(lastIdx);
                        m_listView->scrollTo(lastIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_3) { 
                QModelIndex current = m_listView->currentIndex();
                if (!current.isValid() && m_model->rowCount() > 0) {
                    m_listView->setCurrentIndex(m_model->index(0, 0));
                    return true;
                }

                int row = current.row();
                if (row > 0) {
                    QModelIndex prevIdx = m_model->index(row - 1, 0);
                    m_listView->setCurrentIndex(prevIdx);
                    m_listView->scrollTo(prevIdx);
                } else {
                    
                    

                    if (m_currentPage > 1) {
                        m_currentPage--;
                    } else {
                        m_currentPage = m_totalPages;
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至末页");
                    }
                    refreshData();
                    if (m_model->rowCount() > 0) {
                        QModelIndex lastIdx = m_model->index(m_model->rowCount() - 1, 0);
                        m_listView->setCurrentIndex(lastIdx);
                        m_listView->scrollTo(lastIdx);
                    }
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_4) { 
                QModelIndex current = m_listView->currentIndex();
                if (!current.isValid() && m_model->rowCount() > 0) {
                    m_listView->setCurrentIndex(m_model->index(0, 0));
                    return true;
                }

                int row = current.row();
                if (row < m_model->rowCount() - 1) {
                    QModelIndex nextIdx = m_model->index(row + 1, 0);
                    m_listView->setCurrentIndex(nextIdx);
                    m_listView->scrollTo(nextIdx);
                } else {
                    
                    
        
                    if (m_currentPage < m_totalPages) {
                        m_currentPage++;
                    } else {
                        m_currentPage = 1;
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "已回环至首页");
                    }
                    refreshData();
                    if (m_model->rowCount() > 0) {
                        QModelIndex firstIdx = m_model->index(0, 0);
                        m_listView->setCurrentIndex(firstIdx);
                        m_listView->scrollTo(firstIdx);
                    }
                }
                return true;
            }
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            if (watched == m_searchEdit || watched == m_catSearchEdit || watched == m_tagEdit || watched == m_pageInput) {
                // 2026-04-xx 按照用户要求：Esc 全局清空优先逻辑
                bool searchHasText = m_searchEdit && !m_searchEdit->text().isEmpty();
                bool tagHasText = m_tagEdit && !m_tagEdit->text().isEmpty();
                bool catSearchHasText = m_catSearchEdit && !m_catSearchEdit->text().isEmpty();

                if (searchHasText || tagHasText || catSearchHasText) {
                    if (searchHasText) m_searchEdit->clear();
                    if (tagHasText) m_tagEdit->clear();
                    if (catSearchHasText) m_catSearchEdit->clear();
                    return true;
                } else {
                    m_listView->setFocus();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

#include "QuickWindow.moc"
