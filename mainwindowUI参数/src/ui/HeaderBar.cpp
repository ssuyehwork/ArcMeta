#include "HeaderBar.h"
#include "StringUtils.h"
#include "MainWindow.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include <QHBoxLayout>
#include <QSettings>
#include <QMouseEvent>
#include <QApplication>
#include <QWindow>
#include <QIntValidator>

HeaderBar::HeaderBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(36); 
    setStyleSheet("background-color: #252526; border: none;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部内容区
    auto* topContent = new QWidget();
    auto* layout = new QHBoxLayout(topContent);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignVCenter);
    
    // 1. Logo & Title
    QLabel* appLogo = new QLabel();
    appLogo->setFixedSize(18, 18);
    appLogo->setPixmap(IconHelper::getIcon("zap", "#4a90e2", 18).pixmap(18, 18));
    layout->addWidget(appLogo);
    layout->addSpacing(6);

#ifdef RAPID_MANAGER_TARGET
    QLabel* titleLabel = new QLabel("KingPenguin");
#else
    QLabel* titleLabel = new QLabel("快速笔记");
#endif
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; border: none; background: transparent;");
    layout->addWidget(titleLabel);
    layout->addSpacing(15);

    // 2. Search Box
    m_searchEdit = new SearchLineEdit();
    m_searchEdit->setPlaceholderText("搜索灵感 (双击查看历史)");
    m_searchEdit->setFixedWidth(280);
    m_searchEdit->setFixedHeight(24);
    m_searchEdit->setStyleSheet(
        "SearchLineEdit { "
        "  background-color: #1e1e1e; "
        "  border: 1px solid #444; "
        "  border-radius: 6px; "
        "  padding: 2px 12px; "
        "  color: white; "
        "  font-size: 12px; "
        "} "
        "SearchLineEdit:focus { border: 1px solid #4a90e2; background-color: #181818; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HeaderBar::searchChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){
        m_searchEdit->addHistoryEntry(m_searchEdit->text().trimmed());
    });
    layout->addWidget(m_searchEdit);
    layout->addSpacing(15);

    // 3. Pagination Controls (保持原有逻辑)
    QString pageBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    border-radius: 4px;"
        "    width: 24px;"
        "    height: 24px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:disabled { background-color: transparent; }";

    auto createPageBtn = [&](const QString& icon, const QString& tip) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 16));
        btn->setProperty("tooltipText", tip); btn->installEventFilter(this);
        btn->setStyleSheet(pageBtnStyle);
        return btn;
    };

    QPushButton* btnFirst = createPageBtn("nav_first", "第一页");
    connect(btnFirst, &QPushButton::clicked, [this](){ emit pageChanged(1); });
    layout->addWidget(btnFirst);
    layout->addSpacing(6);

    QPushButton* btnPrev = createPageBtn("nav_prev", "上一页");
    connect(btnPrev, &QPushButton::clicked, [this](){ if(m_currentPage > 1) emit pageChanged(m_currentPage - 1); });
    layout->addWidget(btnPrev);
    layout->addSpacing(8);

    m_pageInput = new QLineEdit("1");
    m_pageInput->setFixedWidth(40);
    m_pageInput->setFixedHeight(24);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setValidator(new QIntValidator(1, 99999, this));
    m_pageInput->setStyleSheet(
        "QLineEdit {"
        "    background-color: #2D2D2D;"
        "    border: 1px solid #555;"
        "    border-radius: 4px;"
        "    color: #eee;"
        "    font-size: 11px;"
        "    padding: 0px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    connect(m_pageInput, &QLineEdit::returnPressed, [this](){
        emit pageChanged(m_pageInput->text().toInt());
    });
    layout->addWidget(m_pageInput);
    layout->addSpacing(6);

    m_totalPageLabel = new QLabel("/ 1");
    m_totalPageLabel->setStyleSheet("color: #888; font-size: 12px; margin-left: 2px; margin-right: 5px; border: none; background: transparent;");
    layout->addWidget(m_totalPageLabel);
    layout->addSpacing(10);

    QPushButton* btnNext = createPageBtn("nav_next", "下一页");
    connect(btnNext, &QPushButton::clicked, [this](){ if(m_currentPage < m_totalPages) emit pageChanged(m_currentPage + 1); });
    layout->addWidget(btnNext);
    layout->addSpacing(6);

    QPushButton* btnLast = createPageBtn("nav_last", "最后一页");
    connect(btnLast, &QPushButton::clicked, [this](){ emit pageChanged(m_totalPages); });
    layout->addWidget(btnLast);
    layout->addSpacing(10);

    QPushButton* btnRefresh = createPageBtn("refresh", "刷新 (F5)");
    connect(btnRefresh, &QPushButton::clicked, this, &HeaderBar::refreshRequested);
    layout->addWidget(btnRefresh);
    layout->addSpacing(10);

    // 标准功能按钮样式 (精简风格，高亮区域由 28x28 缩小至 24x24，保持 4px 弧度以对齐 QuickWindow)
    QString funcBtnStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    outline: none;"
        "    border-radius: 4px;"
        "    width: 24px;"
        "    height: 24px;"
        "    padding: 0px;"
        "}"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }";

#ifndef RAPID_MANAGER_TARGET
    // 迁移：工具箱 和 全局锁定 按钮保持在中间组
    m_btnToolbox = new QPushButton();
    m_btnToolbox->setIcon(IconHelper::getIcon("toolbox", "#aaaaaa", 18));
    m_btnToolbox->setIconSize(QSize(18, 18));
    m_btnToolbox->setProperty("tooltipText", "工具箱"); m_btnToolbox->installEventFilter(this);
    m_btnToolbox->setStyleSheet(funcBtnStyle);
    m_btnToolbox->setContextMenuPolicy(Qt::NoContextMenu);
    connect(m_btnToolbox, &QPushButton::clicked, this, &HeaderBar::toolboxRequested);
    layout->addWidget(m_btnToolbox, 0, (Qt::Alignment)Qt::AlignCenter);
    layout->addSpacing(4);

    QPushButton* btnLock = new QPushButton();
    btnLock->setIcon(IconHelper::getIcon("lock", "#aaaaaa", 18));
    btnLock->setIconSize(QSize(18, 18));
    // 2026-03-xx 按照用户要求，同步更新全局锁定热键提示
    btnLock->setProperty("tooltipText", "全局锁定 （Ctrl + Shift + Alt + S）"); btnLock->installEventFilter(this);
    btnLock->setStyleSheet(funcBtnStyle);
    connect(btnLock, &QPushButton::clicked, this, &HeaderBar::globalLockRequested);
    layout->addWidget(btnLock, 0, (Qt::Alignment)Qt::AlignCenter);
#else
    m_btnToolbox = nullptr;
#endif
    layout->addStretch();

    // 4. 右侧功能与控制按钮组
    // 2026-03-xx 按照用户要求，严格执行“关闭 → 最大化 → 最小化 → 置顶 → 编辑”从右到左的物理顺序。

    // 【编辑/新建】按钮 (位置 5，从右往左)
    QPushButton* btnAddCenter = new QPushButton();
    btnAddCenter->setIcon(IconHelper::getIcon("add", "#aaaaaa", 18));
    btnAddCenter->setIconSize(QSize(18, 18));
    btnAddCenter->setProperty("tooltipText", "新建数据"); btnAddCenter->installEventFilter(this);
    btnAddCenter->setStyleSheet(funcBtnStyle + " QPushButton::menu-indicator { width: 0px; image: none; }");
    
    QMenu* addMenu = new QMenu(this);
    IconHelper::setupMenu(addMenu);
    addMenu->setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                           "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
                           "QMenu::icon { margin-left: 6px; } "
                           "QMenu::item:selected { background-color: #3E3E42; }");
    
    addMenu->addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建数据", [this](){
        if (auto* mw = qobject_cast<MainWindow*>(window())) {
            mw->doNewIdea();
        }
    });
    
    QMenu* createByLineMenu = addMenu->addMenu(IconHelper::getIcon("list_ul", "#aaaaaa", 18), "按行创建数据");
    createByLineMenu->setStyleSheet(addMenu->styleSheet());
    createByLineMenu->addAction("从复制的内容创建", [this](){
        if (auto* mw = qobject_cast<MainWindow*>(window())) {
            mw->doCreateByLine(true);
        }
    });
    createByLineMenu->addAction("从选中数据创建", [this](){
        if (auto* mw = qobject_cast<MainWindow*>(window())) {
            mw->doCreateByLine(false);
        }
    });
    
    btnAddCenter->setMenu(addMenu);
    connect(btnAddCenter, &QPushButton::clicked, [btnAddCenter](){
        btnAddCenter->showMenu();
    });

    // 元数据与筛选 (属于功能增强按钮，放在编辑/新建左侧)
    m_btnFilter = new QPushButton();
    m_btnFilter->setIcon(IconHelper::getIcon("filter", "#aaaaaa", 18));
    m_btnFilter->setIconSize(QSize(18, 18));
    m_btnFilter->setProperty("tooltipText", "筛选器 (Ctrl+G)"); m_btnFilter->installEventFilter(this);
    m_btnFilter->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    m_btnFilter->setCheckable(true);
    connect(m_btnFilter, &QPushButton::clicked, this, &HeaderBar::filterRequested);

    m_btnMeta = new QPushButton();
    m_btnMeta->setIcon(IconHelper::getIcon("sidebar_right", "#aaaaaa", 18));
    m_btnMeta->setIconSize(QSize(18, 18));
    m_btnMeta->setProperty("tooltipText", "元数据面板 (Ctrl+I)"); m_btnMeta->installEventFilter(this);
    m_btnMeta->setCheckable(true);
    m_btnMeta->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(m_btnMeta, &QPushButton::toggled, this, &HeaderBar::metadataToggled);

    // 【置顶】按钮 (位置 4，从右往左)
    m_btnStayOnTop = new QPushButton();
    m_btnStayOnTop->setObjectName("btnStayOnTop");
    m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa", 18));
    m_btnStayOnTop->setIconSize(QSize(18, 18));
    // [USER_REQUEST] 2026-03-xx 统一窗口置顶快捷键为 Alt+Q
    m_btnStayOnTop->setProperty("tooltipText", "始终最前 （Alt + Q）"); m_btnStayOnTop->installEventFilter(this);
    m_btnStayOnTop->setCheckable(true);
    // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后背景为浅灰色，图标变为橙色。
    m_btnStayOnTop->setStyleSheet(funcBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");
    connect(m_btnStayOnTop, &QPushButton::toggled, this, [this](bool checked){
        m_btnStayOnTop->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa", 18));
        emit stayOnTopRequested(checked);
    });

    // 组装右侧：2026-03-xx 按照用户要求，严格执行“关闭 → 最大化 → 最小化 → 置顶 → 编辑”从右到左的物理顺序。
    layout->addWidget(m_btnFilter, 0, (Qt::Alignment)Qt::AlignCenter);
    layout->addSpacing(4);
    layout->addWidget(m_btnMeta, 0, (Qt::Alignment)Qt::AlignCenter);
    layout->addSpacing(4);
    layout->addWidget(btnAddCenter, 0, (Qt::Alignment)Qt::AlignCenter); // 编辑 (位置 5)
    layout->addSpacing(4);
    layout->addWidget(m_btnStayOnTop, 0, (Qt::Alignment)Qt::AlignCenter); // 置顶 (位置 4)
    layout->addSpacing(4);

    // 【窗口控制】组 (从左往右添加，物理视觉为：最小化(3) -> 最大化(2) -> 关闭(1))
    auto addWinBtn = [&](const QString& icon, const QString& hoverColor, auto signal) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(icon, "#aaaaaa", 18));
        btn->setIconSize(QSize(18, 18));
        // 2026-03-xx [NEW] 按照用户要求：窗口控制按钮高亮区域也缩小至 24x24，保持 4px 圆角
        btn->setFixedSize(24, 24); 
        btn->setStyleSheet(QString("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background: %1; }").arg(hoverColor));
        connect(btn, &QPushButton::clicked, this, signal);
        layout->addWidget(btn, 0, (Qt::Alignment)Qt::AlignCenter);
    };

    addWinBtn("minimize", "rgba(255,255,255,0.1)", &HeaderBar::windowMinimize);
    layout->addSpacing(4);
    addWinBtn("maximize", "rgba(255,255,255,0.1)", &HeaderBar::windowMaximize);
    layout->addSpacing(4);
    addWinBtn("close", "#e81123", &HeaderBar::windowClose);

    mainLayout->addWidget(topContent);

    // 【关键修复】实体的 1px 全宽分割线
    auto* bottomLine = new QFrame();
    bottomLine->setFrameShape(QFrame::HLine);
    bottomLine->setFixedHeight(1);
    bottomLine->setStyleSheet("background-color: #333333; border: none; margin: 0px;");
    mainLayout->addWidget(bottomLine);
}

void HeaderBar::updatePagination(int current, int total) {
    m_currentPage = current;
    m_totalPages = total;
    m_pageInput->setText(QString::number(current));
    m_totalPageLabel->setText(QString("/ %1").arg(total));
}

void HeaderBar::setFilterActive(bool active) {
    m_btnFilter->setChecked(active);
}

void HeaderBar::setMetadataActive(bool active) {
    m_btnMeta->setChecked(active);
}

void HeaderBar::updateToolboxStatus(bool active) {
    // 2026-03-22 [NEW] 按照用户要求：激活状态显示绿色 (#00A650)，否则恢复默认灰色 (#aaaaaa)
    if (m_btnToolbox) {
        m_btnToolbox->setIcon(IconHelper::getIcon("toolbox", active ? "#00A650" : "#aaaaaa", 18));
    }
}

void HeaderBar::focusSearch() {
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void HeaderBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* win = window()) {
            if (auto* handle = win->windowHandle()) {
                handle->startSystemMove();
            }
        }
        event->accept();
    }
}

bool HeaderBar::eventFilter(QObject* watched, QEvent* event) {
    // [MODIFIED] 2026-03-xx 物理级拦截原生 ToolTip
    if (event->type() == QEvent::ToolTip) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
        return true; 
    }

    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    return QWidget::eventFilter(watched, event);
}

void HeaderBar::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
}

void HeaderBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit windowMaximize();
        event->accept();
    }
}
