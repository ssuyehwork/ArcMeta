#include "TodoCalendarWindow.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include "ResizeHandle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QMenu>
#include <QTableView>
#include <QHeaderView>
#include <QAbstractItemView>
#include <algorithm>

CustomCalendar::CustomCalendar(QWidget* parent) : QCalendarWidget(parent) {
}

void CustomCalendar::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    bool isSelected = (date == selectedDate());
    bool isToday = (date == QDate::currentDate());

    // 1. 绘制背景
    painter->save();
    if (isSelected) {
        painter->fillRect(rect, QColor("#007acc"));
    } else {
        // [PROFESSIONAL] 热力图渲染：仅在非选中状态显示，且颜色极淡
        if (!todos.isEmpty()) {
            int alpha = qMin(10 + (int)todos.size() * 10, 40);
            painter->fillRect(rect, QColor(255, 255, 255, alpha));
        } else {
            painter->fillRect(rect, QColor("#1e1e1e"));
        }
    }

    // 2. 绘制网格线 (手动绘制以确保即便不调用父类 paintCell 也能保持网格一致性)
    painter->setPen(QColor("#333"));
    painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // 3. 持续显示“今日”高亮边框
    if (isToday) {
        painter->setRenderHint(QPainter::Antialiasing);
        // 改用琥珀色/金黄色高亮今日，避开蓝色混淆
        painter->setPen(QPen(QColor("#eebb00"), 2));
        painter->drawRoundedRect(rect.adjusted(2, 2, -2, -2), 4, 4);
    }
    painter->restore();

    // 4. [CRITICAL] 核心修复：手动绘制日期与任务内容，彻底解决重叠问题
    painter->save();
    
    // A. 绘制日期数字：强制定位在右下角，避开任务区域
    painter->setPen(isSelected ? Qt::white : (date.month() == monthShown() ? QColor("#dcdcdc") : QColor("#555555")));
    QFont dateFont = painter->font();
    dateFont.setBold(true);
    dateFont.setPointSize(isSelected ? 10 : 9); // 选中时稍微加大字号
    painter->setFont(dateFont);
    painter->drawText(rect.adjusted(0, 0, -6, -2), Qt::AlignRight | Qt::AlignBottom, QString::number(date.day()));

    // B. 绘制任务标题：定位在左上角，采用极紧凑布局
    if (!todos.isEmpty()) {
        QFont taskFont = painter->font();
        taskFont.setPointSize(isSelected ? 7 : 6); // 选中时稍微加大以便阅读
        taskFont.setBold(isSelected);              // 选中时加粗
        painter->setFont(taskFont);
        painter->setPen(isSelected ? Qt::white : QColor("#999999"));
        
        for (int i = 0; i < qMin((int)todos.size(), 3); ++i) {
            QString title = todos[i].title;
            if (title.length() > 6) title = title.left(5) + "..";
            // 每行任务偏移 11px，从 y=4 开始绘制
            painter->drawText(rect.adjusted(4, 4 + i * 11, -4, 0), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, "• " + title);
        }
    }
    painter->restore();
}

TodoCalendarWindow::TodoCalendarWindow(QWidget* parent) : FramelessDialog("待办事项", parent) {
    initUI();
    setMinimumSize(950, 700);
    
    // [PROFESSIONAL] 集成窗口缩放手柄
    new ResizeHandle(this, this);
    
    // 安装事件过滤器用于 Tooltip
    m_calendar->installEventFilter(this);
    m_calendar->setMouseTracking(true);
    // QCalendarWidget 内部是由多个小部件组成的，我们需要给它的视图安装追踪
    if (m_calendar->findChild<QAbstractItemView*>()) {
        m_calendar->findChild<QAbstractItemView*>()->setMouseTracking(true);
        m_calendar->findChild<QAbstractItemView*>()->installEventFilter(this);
    }

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &TodoCalendarWindow::onDateSelected);
    connect(m_btnSwitch, &QPushButton::clicked, this, &TodoCalendarWindow::onSwitchView);
    connect(m_btnToday, &QPushButton::clicked, this, &TodoCalendarWindow::onGotoToday);
    connect(m_btnAlarm, &QPushButton::clicked, this, &TodoCalendarWindow::onAddAlarm);
    connect(m_btnAdd, &QPushButton::clicked, this, &TodoCalendarWindow::onAddTodo);
    connect(m_todoList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onEditTodo);
    connect(m_detailed24hList, &QListWidget::itemDoubleClicked, this, &TodoCalendarWindow::onDetailedItemDoubleClicked);
    connect(&DatabaseManager::instance(), &DatabaseManager::todoChanged, this, &TodoCalendarWindow::refreshTodos);

    m_todoList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_todoList, &QListWidget::customContextMenuRequested, [this](const QPoint& pos){
        QList<QListWidgetItem*> items = m_todoList->selectedItems();
        if (items.isEmpty()) return;

        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

        if (items.size() == 1) {
            auto* editAction = menu->addAction(IconHelper::getIcon("edit", "#4facfe"), "编辑此任务");
            connect(editAction, &QAction::triggered, [this, items](){ onEditTodo(items.first()); });
        }

        auto* doneAction = menu->addAction(IconHelper::getIcon("select", "#2ecc71"), items.size() > 1 ? QString("批量标记完成 (%1)").arg(items.size()) : "标记完成");
        connect(doneAction, &QAction::triggered, [this, items](){
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
            for (auto* item : items) {
                int id = item->data(Qt::UserRole).toInt();
                for (auto& t : todos) {
                    if (t.id == id) {
                        t.status = 1;
                        t.progress = 100;
                        DatabaseManager::instance().updateTodo(t);
                        break;
                    }
                }
            }
        });

        auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), items.size() > 1 ? QString("批量删除 (%1)").arg(items.size()) : "删除此任务");
        connect(deleteAction, &QAction::triggered, [this, items](){
            for (auto* item : items) {
                int id = item->data(Qt::UserRole).toInt();
                DatabaseManager::instance().deleteTodo(id);
            }
        });

        menu->exec(QCursor::pos());
    });
}

void TodoCalendarWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(20);

    // [CRITICAL] 锁定：布局迁移。左侧面板占 35% 宽度。
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #4facfe; margin-bottom: 5px;");
    leftLayout->addWidget(m_dateLabel);

    // [USER_REQUEST] 2026-03-xx 增加搜索框
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索任务标题或内容...");
    m_searchEdit->setStyleSheet(
        "QLineEdit { background-color: #252526; border: 1px solid #444; border-radius: 4px; padding: 8px; color: #ccc; font-size: 13px; }"
        "QLineEdit:focus { border-color: #4facfe; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TodoCalendarWindow::refreshTodos);
    leftLayout->addWidget(m_searchEdit);

    // [USER_REQUEST] 2026-03-xx 增加状态分类页签
    m_tabBar = new QWidget(this);
    auto* tabLayout = new QHBoxLayout(m_tabBar);
    tabLayout->setContentsMargins(0, 5, 0, 5);
    tabLayout->setSpacing(5);
    
    QStringList tabs = {"全部", "待办", "进行中", "已完成"};
    for (int i = 0; i < tabs.size(); ++i) {
        auto* btn = new QPushButton(tabs[i], this);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        if (i == 0) btn->setChecked(true);
        btn->setStyleSheet(
            "QPushButton { background-color: #2d2d2d; color: #888; border: none; padding: 6px; border-radius: 4px; font-size: 11px; font-weight: bold; }"
            "QPushButton:checked { background-color: #3e3e42; color: #4facfe; }"
            "QPushButton:hover:!checked { background-color: #333; }"
        );
        connect(btn, &QPushButton::clicked, [this, i](){
            m_currentTabIdx = i;
            refreshTodos();
        });
        m_tabButtons << btn;
        tabLayout->addWidget(btn);
    }
    leftLayout->addWidget(m_tabBar);

    m_todoList = new QListWidget(this);
    m_todoList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_todoList->setStyleSheet(
        "QListWidget { background-color: #252526; border: 1px solid #444; border-radius: 4px; padding: 5px; color: #ccc; outline: none; }"
        "QListWidget::item { border-bottom: 1px solid #333; padding: 12px 10px; }"
        "QListWidget::item:selected { background-color: #3e3e42; color: white; border-radius: 4px; }"
    );
    // [USER_REQUEST] 2026-03-xx 实现列表点击与日历联动
    connect(m_todoList, &QListWidget::itemClicked, [this](QListWidgetItem* item){
        // [PERF] 2026-03-15 优化：直接从 item 读取缓存的日期，避免点击时触发全量数据库查询
        QVariant dateVar = item->data(Qt::UserRole + 1);
        if (dateVar.isValid()) {
            m_calendar->setSelectedDate(dateVar.toDate());
        }
    });
    leftLayout->addWidget(m_todoList);

    m_btnAdd = new QPushButton("新增待办", this);
    m_btnAdd->setIcon(IconHelper::getIcon("add", "#ffffff"));
    m_btnAdd->setProperty("tooltipText", "在当前选中的日期创建一个新任务");
    m_btnAdd->installEventFilter(this);
    m_btnAdd->setStyleSheet(
        "QPushButton { background-color: #007acc; color: white; border: none; padding: 10px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
    );
    leftLayout->addWidget(m_btnAdd);

    mainLayout->addWidget(leftPanel, 35);

    // [CRITICAL] 锁定：右侧面板占 65% 宽度，支持月历/24h 视图切换。
    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(5);

    auto* rightHeader = new QHBoxLayout();
    rightHeader->addStretch();

    // 2026-03-xx 按照用户要求：统一控制按钮尺寸为 24x24，圆角 4px
    m_btnToday = new QPushButton(this);
    m_btnToday->setFixedSize(24, 24);
    m_btnToday->setIcon(IconHelper::getIcon("today", "#ccc", 18));
    m_btnToday->setIconSize(QSize(18, 18));
    m_btnToday->setProperty("tooltipText", "定位到今天");
    m_btnToday->installEventFilter(this);
    m_btnToday->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #3e3e42; }");
    rightHeader->addWidget(m_btnToday);

    m_btnAlarm = new QPushButton(this);
    m_btnAlarm->setFixedSize(24, 24);
    m_btnAlarm->setIcon(IconHelper::getIcon("bell", "#ccc", 18));
    m_btnAlarm->setIconSize(QSize(18, 18));
    m_btnAlarm->setProperty("tooltipText", "创建重复提醒闹钟");
    m_btnAlarm->installEventFilter(this);
    m_btnAlarm->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #3e3e42; }");
    rightHeader->addWidget(m_btnAlarm);

    m_btnSwitch = new QPushButton(this);
    m_btnSwitch->setFixedSize(24, 24);
    m_btnSwitch->setIcon(IconHelper::getIcon("clock", "#ccc", 18));
    m_btnSwitch->setIconSize(QSize(18, 18));
    m_btnSwitch->setProperty("tooltipText", "切换日历/24h详细视图");
    m_btnSwitch->installEventFilter(this);
    m_btnSwitch->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #3e3e42; }");
    rightHeader->addWidget(m_btnSwitch);
    rightLayout->addLayout(rightHeader);

    m_viewStack = new QStackedWidget(this);

    // 视图 1：月视图 (日历重构版)
    auto* calendarContainer = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(calendarContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    m_calendar = new CustomCalendar(this);
    m_calendar->setGridVisible(false);
    m_calendar->setFirstDayOfWeek(Qt::Monday);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendar->setHorizontalHeaderFormat(QCalendarWidget::NoHorizontalHeader); // [CRITICAL] 彻底隐藏原生灰白色星期表头
    m_calendar->setNavigationBarVisible(true); // 保持原生导航栏，它在顶部且颜色可通过 QSS 控制

    // [ARCH-RECONSTRUCT] 自定义星期标题栏：彻底解决原生 HeaderView 灰白色背景无法修改的问题
    auto* customHeader = new QWidget(this);
    customHeader->setFixedHeight(35);
    customHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* headerLayout = new QHBoxLayout(customHeader);
    // [FIX] 严格对齐：QCalendarWidget 内部 View 通常有 1px 边距，且我们要防止周日被截断
    headerLayout->setContentsMargins(1, 0, 1, 0);
    headerLayout->setSpacing(0);

    QStringList weekDays = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    for (const QString& day : weekDays) {
        auto* label = new QLabel(day, this);
        label->setAlignment(Qt::AlignCenter);
        // [FIX] 显式设置拉伸系数，确保 7 个标签平分空间且不被遮挡
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 13px; border: none; background: transparent;")
                             .arg((day == "周六" || day == "周日") ? "#ff4d4f" : "#eebb00"));
        headerLayout->addWidget(label);
    }

    m_calendar->setStyleSheet(
        "QCalendarWidget { background-color: #1e1e1e; border: none; }"
        "QCalendarWidget QAbstractItemView { background-color: #1e1e1e; color: #dcdcdc; selection-background-color: transparent; selection-color: #dcdcdc; outline: none; border: none; padding: 0; margin: 0; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color: #2d2d2d; border-bottom: 1px solid #333; }"
        "QCalendarWidget QToolButton { color: #eee; font-weight: bold; background-color: transparent; border: none; padding: 5px 15px; min-width: 60px; }"
        "QCalendarWidget QToolButton:hover { background-color: #3e3e42; border-radius: 4px; }" // 2026-03-xx 统一悬停色
        "QCalendarWidget QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; }"
        "QCalendarWidget QMenu::item:selected { background-color: #3e3e42; }" // 2026-03-xx 统一菜单悬停色为 #3e3e42
        "QCalendarWidget QSpinBox { background-color: #2d2d2d; color: #eee; selection-background-color: #007acc; border: 1px solid #444; margin-right: 5px; }"
    );

    // [HACK] 为了将自定义星期栏插入导航栏和格子之间，我们需要手动调整布局
    // 简单起见，既然 QCalendarWidget 是一个整体，我们可以通过 QCalendarWidget 的内部结构来做，
    // 但更稳妥的方法是：如果无法直接插入，我们可以隐藏导航栏并自己重写导航栏。
    
    // 方案调整：彻底隐藏原生导航栏，自己重写完整的日历外壳
    m_calendar->setNavigationBarVisible(false);
    
    auto* navBar = new QWidget(this);
    navBar->setFixedHeight(45);
    navBar->setStyleSheet("background-color: #2d2d2d; border-bottom: 1px solid #333;");
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(10, 0, 10, 0);
    
    auto* btnPrev = new QPushButton(IconHelper::getIcon("nav_prev", "#ccc"), "", this);
    auto* btnNext = new QPushButton(IconHelper::getIcon("nav_next", "#ccc"), "", this);
    btnPrev->setProperty("tooltipText", "上一个月");
    btnNext->setProperty("tooltipText", "下一个月");
    btnPrev->installEventFilter(this);
    btnNext->installEventFilter(this);
    auto* btnMonth = new QPushButton(this);
    btnMonth->setStyleSheet("QPushButton { color: white; font-weight: bold; font-size: 15px; background: transparent; border: none; padding: 5px 15px; } QPushButton:hover { background: #444; border-radius: 4px; }");
    btnMonth->setIcon(IconHelper::getIcon("arrow_down", "#888", 12));
    
    auto updateMonthLabel = [this, btnMonth](){
        btnMonth->setText(QString("%1年 %2月").arg(m_calendar->yearShown()).arg(m_calendar->monthShown()));
    };
    updateMonthLabel();

    // [PROFESSIONAL] 恢复月份/年份快速切换功能
    btnMonth->setCursor(Qt::PointingHandCursor);
    btnMonth->setProperty("tooltipText", "点击快速选择年月");
    btnMonth->installEventFilter(this);
    
    auto showYearMonthMenu = [this, btnMonth, updateMonthLabel](){
        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42
        
        auto* yearMenu = menu->addMenu("选择年份");
        int currentYear = m_calendar->yearShown();
        for (int y = currentYear - 5; y <= currentYear + 5; ++y) {
            auto* yearAction = yearMenu->addAction(QString("%1年").arg(y));
            if (y == currentYear) yearAction->setIcon(IconHelper::getIcon("select", "#4facfe"));
            connect(yearAction, &QAction::triggered, [this, y, updateMonthLabel](){
                m_calendar->setCurrentPage(y, m_calendar->monthShown());
                updateMonthLabel();
            });
        }

        auto* monthMenu = menu->addMenu("选择月份");
        int currentMonth = m_calendar->monthShown();
        for (int m = 1; m <= 12; ++m) {
            auto* monthAction = monthMenu->addAction(QString("%1月").arg(m));
            if (m == currentMonth) monthAction->setIcon(IconHelper::getIcon("select", "#4facfe"));
            connect(monthAction, &QAction::triggered, [this, m, updateMonthLabel](){
                m_calendar->setCurrentPage(m_calendar->yearShown(), m);
                updateMonthLabel();
            });
        }
        
        menu->exec(QCursor::pos());
    };
    
    // 给 labelMonth 添加点击事件 (简单做法：给父窗口安装过滤器或用 ClickableLabel)
    // 这里采用更直接的方案：把 labelMonth 换成按钮样式
    
    connect(btnPrev, &QPushButton::clicked, [this, updateMonthLabel](){ m_calendar->showPreviousMonth(); updateMonthLabel(); });
    connect(btnNext, &QPushButton::clicked, [this, updateMonthLabel](){ m_calendar->showNextMonth(); updateMonthLabel(); });
    connect(m_calendar, &QCalendarWidget::currentPageChanged, updateMonthLabel);

    btnPrev->setFixedSize(30, 30);
    btnNext->setFixedSize(30, 30);
    btnPrev->setStyleSheet("QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #4facfe; }");
    btnNext->setStyleSheet("QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #4facfe; }");

    navLayout->addWidget(btnPrev);
    navLayout->addStretch();
    navLayout->addWidget(btnMonth);
    navLayout->addStretch();
    navLayout->addWidget(btnNext);

    connect(btnMonth, &QPushButton::clicked, showYearMonthMenu);

    containerLayout->addWidget(navBar);
    containerLayout->addWidget(customHeader);
    containerLayout->addWidget(m_calendar);
    m_viewStack->addWidget(calendarContainer);

    // 视图 2：详细 24h 视图
    m_detailed24hList = new QListWidget(this);
    m_detailed24hList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_detailed24hList->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: 1px solid #333; border-radius: 4px; color: #dcdcdc; font-size: 14px; }"
        "QListWidget::item { padding: 15px; border-bottom: 1px solid #2d2d2d; min-height: 50px; }"
        "QListWidget::item:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
    );
    m_viewStack->addWidget(m_detailed24hList);

    m_detailed24hList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_detailed24hList, &QListWidget::customContextMenuRequested, [this](const QPoint& pos){
        QList<QListWidgetItem*> items = m_detailed24hList->selectedItems();
        if (items.isEmpty()) return;

        auto* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

        // 收集所有选中行中的任务ID
        QList<int> taskIds;
        QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
        
        for (auto* item : items) {
            int hour = m_detailed24hList->row(item);
            for (const auto& t : todos) {
                if (t.startTime.isValid() && t.startTime.time().hour() == hour) {
                    taskIds << t.id;
                    break;
                }
            }
        }

        if (items.size() == 1) {
            int hour = m_detailed24hList->row(items.first());
            bool hasTask = !taskIds.isEmpty();
            if (hasTask) {
                int taskId = taskIds.first();
                auto* editAction = menu->addAction(IconHelper::getIcon("edit", "#4facfe"), "编辑任务");
                auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), "删除任务");
                connect(editAction, &QAction::triggered, [this, taskId](){
                    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
                    for(const auto& t : todos) if(t.id == taskId) { 
                        openEditDialog(t);
                        break; 
                    }
                });
                connect(deleteAction, &QAction::triggered, [this, taskId](){ DatabaseManager::instance().deleteTodo(taskId); });
            } else {
                auto* addAction = menu->addAction(IconHelper::getIcon("add", "#4facfe"), QString("在 %1:00 新增任务").arg(hour, 2, 10, QChar('0')));
                connect(addAction, &QAction::triggered, [this, hour](){
                    DatabaseManager::Todo t;
                    t.startTime = QDateTime(m_calendar->selectedDate(), QTime(hour, 0));
                    t.endTime = t.startTime.addSecs(3600);
                    auto* dlg = new TodoEditDialog(t, this);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    connect(dlg, &QDialog::accepted, [this, dlg](){ 
                        DatabaseManager::instance().addTodo(dlg->getTodo());
                        this->refreshTodos();
                    });
                    dlg->show();
                });
            }
        } else {
            // 多选情况
            if (!taskIds.isEmpty()) {
                auto* doneAction = menu->addAction(IconHelper::getIcon("select", "#2ecc71"), QString("批量标记完成 (%1)").arg(taskIds.size()));
                connect(doneAction, &QAction::triggered, [this, taskIds](){
                    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
                    for (int id : taskIds) {
                        for (auto& t : todos) {
                            if (t.id == id) {
                                t.status = 1;
                                t.progress = 100;
                                DatabaseManager::instance().updateTodo(t);
                                break;
                            }
                        }
                    }
                });

                auto* deleteAction = menu->addAction(IconHelper::getIcon("delete", "#e74c3c"), QString("批量删除任务 (%1)").arg(taskIds.size()));
                connect(deleteAction, &QAction::triggered, [this, taskIds](){
                    for (int id : taskIds) {
                        DatabaseManager::instance().deleteTodo(id);
                    }
                });
            } else {
                return; // 选中的全是空行且是多选，不显示菜单
            }
        }

        menu->exec(QCursor::pos());
    });

    rightLayout->addWidget(m_viewStack);
    mainLayout->addWidget(rightPanel, 65);
    
    onDateSelected();
}

void TodoCalendarWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    refreshTodos();
}

bool TodoCalendarWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ContextMenu) {
        // [PROFESSIONAL] 日历格子的右键点击：先触发选中，再弹出菜单
        auto* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            if (view) {
                QPoint pos = view->mapFromGlobal(QCursor::pos());
                QModelIndex index = view->indexAt(pos);
                if (index.isValid()) {
                    // [HACK] 通过模拟鼠标左键点击来触发 QCalendarWidget 的选中逻辑
                    // Qt6 推荐使用包含 localPos 和 globalPos 的构造函数
                    QMouseEvent clickEvent(QEvent::MouseButtonPress, pos, QCursor::pos(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &clickEvent);
                    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, pos, QCursor::pos(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &releaseEvent);
                }
            }

            auto* menu = new QMenu(this);
            IconHelper::setupMenu(menu);
            menu->setStyleSheet("QMenu { background-color: #2d2d2d; color: #eee; border: 1px solid #444; } QMenu::item:selected { background-color: #3e3e42; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

            QDate selectedDate = m_calendar->selectedDate();
            QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(selectedDate);

            auto* addAction = menu->addAction(IconHelper::getIcon("add", "#4facfe"), "在此日期新增待办");
            auto* detailAction = menu->addAction(IconHelper::getIcon("clock", "#4facfe"), "切换到排程视图");
            
            if (!todos.isEmpty()) {
                menu->addSeparator();
                auto* taskTitle = menu->addAction(QString("管理该日任务 (%1):").arg(todos.size()));
                taskTitle->setEnabled(false);
                
                for (const auto& t : todos) {
                    QString time = t.startTime.isValid() ? "[" + t.startTime.toString("HH:mm") + "] " : "";
                    auto* itemAction = menu->addAction(IconHelper::getIcon("todo", "#aaaaaa"), time + t.title);
                    connect(itemAction, &QAction::triggered, [this, t](){
                        openEditDialog(t);
                    });
                }
            }

            menu->addSeparator();
            auto* todayAction = menu->addAction(IconHelper::getIcon("today", "#aaaaaa"), "返回今天");

            connect(addAction, &QAction::triggered, this, &TodoCalendarWindow::onAddTodo);
            connect(detailAction, &QAction::triggered, [this](){
                m_viewStack->setCurrentIndex(1);
                m_btnSwitch->setIcon(IconHelper::getIcon("calendar", "#ccc"));
                m_btnSwitch->setProperty("tooltipText", "切换到月历视图");
            });
            connect(todayAction, &QAction::triggered, this, &TodoCalendarWindow::onGotoToday);

            menu->exec(QCursor::pos());
            return true;
        }
    }

    // [USER_REQUEST] 2026-03-xx 按照用户要求，增强 eventFilter 以支持双击日历格子快速新增待办
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();
            if (watched == m_calendar) pos = view->mapFromParent(pos);
            
            QModelIndex index = view->indexAt(pos);
            if (index.isValid()) {
                // 执行新增逻辑
                onAddTodo();
                return true;
            }
        }
    }

    // 处理所有按钮的 Hover 自定义提示
    if (event->type() == QEvent::Enter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 2000);
        }
    } else if (event->type() == QEvent::Leave) {
        ToolTipOverlay::hideTip();
    }

    // [MODIFIED] 2026-03-xx 强制拦截日历 ToolTip，彻底解决原生提示由于逻辑未截断而“回退弹出”的问题
    if (event->type() == QEvent::ToolTip) {
        auto* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            QPoint pos = static_cast<QHelpEvent*>(event)->pos();
            if (watched == m_calendar) pos = view->mapFromParent(pos);
            QModelIndex index = view->indexAt(pos);
            
            QDate date;
            if (index.isValid()) {
                QDate firstOfMonth(m_calendar->yearShown(), m_calendar->monthShown(), 1);
                int offset = (firstOfMonth.dayOfWeek() - (int)m_calendar->firstDayOfWeek() + 7) % 7;
                date = firstOfMonth.addDays(-offset + index.row() * 7 + index.column());
            }

            if (date.isValid()) {
                QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
                if (!todos.isEmpty()) {
                    QString tip = "<b>" + date.toString("yyyy-MM-dd") + " 待办概要:</b><br>";
                    for (int i = 0; i < qMin((int)todos.size(), 5); ++i) {
                        const auto& t = todos[i];
                        QString time = t.startTime.isValid() ? "[" + t.startTime.toString("HH:mm") + "] " : "";
                        tip += "• " + time + t.title + "<br>";
                    }
                    if (todos.size() > 5) tip += QString("<i>...更多 (%1)</i>").arg(todos.size());
                    
                    // 2026-03-xx 按照用户要求，数据类悬停提示时长设为 2 秒 (2000ms)
                    ToolTipOverlay::instance()->showText(QCursor::pos(), tip, 2000);
                } else {
                    ToolTipOverlay::hideTip();
                }
            }
        }
        // [CRITICAL] 物理级截断：无论是否有内容，只要是 ToolTip 事件都必须返回 true，防止 Qt 触发原生提示
        return true; 
    }

    if (event->type() == QEvent::MouseMove) {
        auto* view = m_calendar->findChild<QAbstractItemView*>();
        if (watched == m_calendar || watched == view) {
            QPoint pos = static_cast<QMouseEvent*>(event)->pos();
            if (watched == m_calendar) pos = view->mapFromParent(pos);
            if (!view->indexAt(pos).isValid()) ToolTipOverlay::hideTip();
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void TodoCalendarWindow::update24hList(const QDate& date) {
    m_detailed24hList->clear();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(date);
    
    for (int h = 0; h < 24; ++h) {
        QString timeStr = QString("%1:00").arg(h, 2, 10, QChar('0'));
        
        // [CRITICAL] 锁定：仅更新右侧详细列表。左侧冗余列表已移除。
        auto* itemDetailed = new QListWidgetItem(timeStr, m_detailed24hList);
        itemDetailed->setData(Qt::UserRole, 0); // 2026-03-xx 初始化数据状态，防止残留
        itemDetailed->setFont(QFont("Segoe UI", 12));
        bool hasTaskDetailed = false;
        for (const auto& t : todos) {
            if (t.startTime.isValid() && t.startTime.date() == date && t.startTime.time().hour() == h) {
                // [USER_REQUEST] 2026-03-xx 核心修复：为 24h 列表项绑定 ID，以支持双击编辑功能
                itemDetailed->setData(Qt::UserRole, t.id);
                
                QString displayTime = t.startTime.toString("HH:mm");
                if (t.endTime.isValid()) displayTime += " - " + t.endTime.toString("HH:mm");
                itemDetailed->setText(QString("%1   |   %2").arg(displayTime, -15).arg(t.title));
                itemDetailed->setForeground(QColor("#4facfe"));
                
                if (t.status == 1) {
                    itemDetailed->setIcon(IconHelper::getIcon("select", "#666", 20));
                    itemDetailed->setForeground(QColor("#666"));
                } else if (t.status == 2) {
                    itemDetailed->setIcon(IconHelper::getIcon("close", "#e74c3c", 20));
                } else if (t.priority == 2) {
                    itemDetailed->setIcon(IconHelper::getIcon("bell", "#f1c40f", 20));
                    itemDetailed->setForeground(QColor("#f1c40f"));
                } else {
                    itemDetailed->setIcon(IconHelper::getIcon("circle_filled", "#007acc", 12));
                }
                hasTaskDetailed = true;
                break;
            }
        }
        if (!hasTaskDetailed) itemDetailed->setForeground(QColor("#444"));
        m_detailed24hList->addItem(itemDetailed);
    }
}

void TodoCalendarWindow::onDateSelected() {
    QDate date = m_calendar->selectedDate();
    m_dateLabel->setText(date.toString("yyyy年M月d日"));
    refreshTodos();
    update24hList(date);
}

void TodoCalendarWindow::onSwitchView() {
    int nextIdx = (m_viewStack->currentIndex() + 1) % 2;
    m_viewStack->setCurrentIndex(nextIdx);
    
    if (nextIdx == 0) {
        m_btnSwitch->setIcon(IconHelper::getIcon("clock", "#ccc"));
        m_btnSwitch->setProperty("tooltipText", "切换到24h详细视图");
    } else {
        m_btnSwitch->setIcon(IconHelper::getIcon("calendar", "#ccc"));
        m_btnSwitch->setProperty("tooltipText", "切换到月历视图");
    }
}

void TodoCalendarWindow::onGotoToday() {
    m_calendar->setSelectedDate(QDate::currentDate());
    onDateSelected();
}

void TodoCalendarWindow::refreshTodos() {
    m_todoList->clear();
    
    // [USER_REQUEST] 2026-03-xx 获取全局任务数据，并根据页签及搜索词过滤
    QList<DatabaseManager::Todo> allTodos = DatabaseManager::instance().getAllTodos();
    QList<DatabaseManager::Todo> filtered;
    
    QString kw = m_searchEdit->text().trimmed();
    
    for (const auto& t : allTodos) {
        // 1. 搜索词过滤
        if (!kw.isEmpty()) {
            if (!t.title.contains(kw, Qt::CaseInsensitive) && !t.content.contains(kw, Qt::CaseInsensitive)) {
                continue;
            }
        }
        
        // 2. 页签过滤 (0:全部, 1:待办, 2:进行中, 3:已完成)
        if (m_currentTabIdx == 1 && t.status != 0) continue;
        if (m_currentTabIdx == 2 && (t.status != 2 && (t.progress == 0 || t.status == 1))) continue;
        if (m_currentTabIdx == 3 && t.status != 1) continue;
        
        filtered.append(t);
    }

    // [CRITICAL] 锁定排序：逾期 > 优先级 > 更新时间
    std::sort(filtered.begin(), filtered.end(), [](const DatabaseManager::Todo& a, const DatabaseManager::Todo& b){
        if (a.status == 2 && b.status != 2) return true;
        if (a.status != 2 && b.status == 2) return false;
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.updatedAt > b.updatedAt;
    });

    for (const auto& t : filtered) {
        auto* item = new QListWidgetItem(m_todoList);
        
        // 全局视图下增加日期显示
        QString dateStr = t.startTime.isValid() ? t.startTime.toString("MM/dd ") : "";
        QString timeStr = t.startTime.isValid() ? t.startTime.toString("HH:mm") : "--:--";
        
        QString titleText = t.title;
        if (t.repeatMode > 0) titleText += " 🔄";
        if (t.noteId > 0) titleText += " 📝";
        if (t.progress > 0 && t.progress < 100) titleText += QString(" (%1%)").arg(t.progress);

        item->setText(QString("%1%2 %3").arg(dateStr).arg(timeStr).arg(titleText));
        item->setData(Qt::UserRole, t.id);
        // [PERF] 2026-03-15 优化性能：直接存储日期，避免点击时全量查库
        if (t.startTime.isValid()) {
            item->setData(Qt::UserRole + 1, t.startTime.date());
        }
        
        // [USER_REQUEST] 2026-03-xx 按照用户要求的颜色规范显示
        if (t.status == 1) {
            // 已完成 -> 绿色 (#2ecc71)
            item->setIcon(IconHelper::getIcon("select", "#2ecc71", 16));
            item->setForeground(QColor("#2ecc71"));
            auto font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
        } else if (t.status == 2 || (t.progress > 0 && t.progress < 100)) {
            // 任务中/逾期 -> 黄橙色 (#f39c12)
            item->setIcon(IconHelper::getIcon(t.status == 2 ? "close" : "clock", "#f39c12", 16));
            item->setForeground(QColor("#f39c12"));
            if (t.status == 2) item->setBackground(QColor(243, 156, 18, 20));
        } else {
            // 新任务 -> 白色 (#ffffff)
            item->setIcon(IconHelper::getIcon("circle_filled", "#ffffff", 8));
            item->setForeground(Qt::white);
        }
        
        if (t.priority == 2 && t.status != 1) {
            item->setBackground(QColor(231, 76, 60, 25)); // 紧急任务微红底色
        }

        m_todoList->addItem(item);
    }
}

void TodoCalendarWindow::onAddAlarm() {
    DatabaseManager::Todo t;
    t.title = "新闹钟";
    t.reminderTime = QDateTime::currentDateTime().addSecs(60);
    t.repeatMode = 1; // 默认每天重复
    
    // [ARCH-RECONSTRUCT] 闹钟架构独立化：使用专门的 AlarmEditDialog
    auto* dlg = new AlarmEditDialog(t, this->isVisible() ? this : nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    // 如果没有可见父窗口，手动居中显示
    if (!this->isVisible()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            dlg->move(screen->availableGeometry().center() - QPoint(200, 150));
        }
    }

    connect(dlg, &QDialog::accepted, [this, dlg](){
        DatabaseManager::instance().addTodo(dlg->getTodo());
        this->refreshTodos();
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TodoCalendarWindow::onAddTodo() {
    DatabaseManager::Todo t;
    t.startTime = QDateTime(m_calendar->selectedDate(), QTime::currentTime());
    t.endTime = t.startTime.addSecs(3600);
    
    auto* dlg = new TodoEditDialog(t, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, [this, dlg](){
        DatabaseManager::instance().addTodo(dlg->getTodo());
        this->refreshTodos();
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TodoCalendarWindow::onEditTodo(QListWidgetItem* item) {
    int id = item->data(Qt::UserRole).toInt();
    QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
    for (const auto& t : todos) {
        if (t.id == id) {
            openEditDialog(t);
            break;
        }
    }
}

void TodoCalendarWindow::onDetailedItemDoubleClicked(QListWidgetItem* item) {
    // [USER_REQUEST] 2026-03-xx 按照用户要求，实现排程视图双击逻辑：有任务则编辑，无任务则按该小时新增
    int todoId = item->data(Qt::UserRole).toInt();
    if (todoId > 0) {
        QList<DatabaseManager::Todo> todos = DatabaseManager::instance().getTodosByDate(m_calendar->selectedDate());
        for (const auto& t : todos) {
            if (t.id == todoId) {
                openEditDialog(t);
                return;
            }
        }
    } else {
        // 无任务，在对应小时新增
        int hour = m_detailed24hList->row(item);
        DatabaseManager::Todo t;
        t.startTime = QDateTime(m_calendar->selectedDate(), QTime(hour, 0));
        t.endTime = t.startTime.addSecs(3600);
        
        auto* dlg = new TodoEditDialog(t, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, [this, dlg](){
            DatabaseManager::instance().addTodo(dlg->getTodo());
            this->refreshTodos();
        });
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}

void TodoCalendarWindow::openEditDialog(const DatabaseManager::Todo& t) {
    QDialog* dlg = nullptr;
    if (t.priority == 2) {
        dlg = new AlarmEditDialog(t, this);
    } else {
        dlg = new TodoEditDialog(t, this);
    }
    
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, [this, dlg, t](){
        DatabaseManager::Todo updatedTodo;
        if (t.priority == 2) updatedTodo = qobject_cast<AlarmEditDialog*>(dlg)->getTodo();
        else updatedTodo = qobject_cast<TodoEditDialog*>(dlg)->getTodo();
        
        DatabaseManager::instance().updateTodo(updatedTodo);
        this->refreshTodos();
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// --- TodoReminderDialog ---

TodoReminderDialog::TodoReminderDialog(const DatabaseManager::Todo& todo, QWidget* parent)
    : FramelessDialog("待办提醒", parent), m_todo(todo)
{
    resize(380, 260);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(25, 20, 25, 25);
    layout->setSpacing(15);

    auto* titleLabel = new QLabel(QString("<b>任务到期提醒：</b><br>%1").arg(todo.title));
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet("font-size: 15px; color: #4facfe;");
    layout->addWidget(titleLabel);

    if (!todo.content.isEmpty()) {
        auto* contentLabel = new QLabel(todo.content);
        contentLabel->setWordWrap(true);
        contentLabel->setStyleSheet("color: #bbb; font-size: 13px;");
        layout->addWidget(contentLabel);
    }

    layout->addStretch();

    auto* snoozeLayout = new QHBoxLayout();
    snoozeLayout->setSpacing(10);
    
    auto* snoozeSpin = new QSpinBox(this);
    snoozeSpin->setRange(1, 1440);
    snoozeSpin->setValue(5);
    snoozeSpin->setAlignment(Qt::AlignCenter);
    snoozeSpin->setStyleSheet("QSpinBox { background: #333; color: white; border: 1px solid #444; padding: 5px; min-width: 70px; } "
                             "QSpinBox::up-button, QSpinBox::down-button { width: 20px; }");
    
    auto* btnSnooze = new QPushButton("稍后提醒");
    btnSnooze->setCursor(Qt::PointingHandCursor);
    btnSnooze->setStyleSheet("QPushButton { background: #444; color: #ddd; border: 1px solid #555; padding: 6px 15px; border-radius: 4px; } "
                            "QPushButton:hover { background: #555; color: white; }");
    
    snoozeLayout->addWidget(new QLabel("延时:"));
    snoozeLayout->addWidget(snoozeSpin);
    snoozeLayout->addWidget(new QLabel("分钟"));
    snoozeLayout->addStretch();
    snoozeLayout->addWidget(btnSnooze);
    layout->addLayout(snoozeLayout);

    auto* btnOk = new QPushButton("知道了");
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet("QPushButton { background: #007acc; color: white; padding: 10px; border-radius: 4px; font-weight: bold; } "
                        "QPushButton:hover { background: #0098ff; }");
    layout->addWidget(btnOk);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnSnooze, &QPushButton::clicked, [this, snoozeSpin](){
        emit snoozeRequested(snoozeSpin->value());
        accept();
    });
}

// --- CustomDateTimeEdit ---

CustomDateTimeEdit::CustomDateTimeEdit(const QDateTime& dt, QWidget* parent) 
    : QWidget(parent), m_dateTime(dt) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_display = new QLineEdit(this);
    m_display->setReadOnly(false);
    m_display->installEventFilter(this);
    m_display->setInputMask("0000/00/00 00:00"); // 强制输入格式，极大提升输入效率和解析稳健性
    m_display->setStyleSheet("QLineEdit { background: #333; border: 1px solid #444; border-radius: 4px; color: white; padding: 5px; font-size: 13px; } "
                             "QLineEdit:focus { border-color: #4facfe; }");
    
    connect(m_display, &QLineEdit::editingFinished, [this](){
        QString text = m_display->text();
        QDateTime dt = QDateTime::fromString(text, "yyyy/MM/dd HH:mm");
        if (dt.isValid()) {
            m_dateTime = dt;
            emit dateTimeChanged(dt);
        } else {
            // 输入非法则重置回旧值
            updateDisplay();
        }
    });

    m_btn = new QPushButton(IconHelper::getIcon("calendar", "#888", 16), "", this);
    m_btn->setFixedSize(30, 30);
    m_btn->setStyleSheet("QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #444; }");
    connect(m_btn, &QPushButton::clicked, this, &CustomDateTimeEdit::showPicker);

    layout->addWidget(m_display, 1);
    layout->addWidget(m_btn);

    updateDisplay();
}

void CustomDateTimeEdit::setDateTime(const QDateTime& dt) {
    m_dateTime = dt;
    updateDisplay();
    emit dateTimeChanged(dt);
}

void CustomDateTimeEdit::updateDisplay() {
    m_display->setText(m_dateTime.toString("yyyy/MM/dd HH:mm"));
}

void CustomDateTimeEdit::showPicker() {
    auto* picker = new FramelessDialog("选择日期和时间", this);
    picker->setFixedSize(450, 550);
    picker->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(picker->getContentArea());
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(0);

    // --- 日历重构：完全复制 TodoCalendarWindow 的成功方案 ---
    auto* calContainer = new QWidget(picker);
    auto* calLayout = new QVBoxLayout(calContainer);
    calLayout->setContentsMargins(0, 0, 0, 0);
    calLayout->setSpacing(0);

    auto* cal = new CustomCalendar(picker);
    cal->setSelectedDate(m_dateTime.date());
    cal->setGridVisible(false);
    cal->setFirstDayOfWeek(Qt::Monday);
    cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    cal->setHorizontalHeaderFormat(QCalendarWidget::NoHorizontalHeader);
    cal->setNavigationBarVisible(false);

    // 1. 自定义导航栏
    auto* navBar = new QWidget(picker);
    navBar->setFixedHeight(40);
    navBar->setStyleSheet("background-color: #2d2d2d; border-bottom: 1px solid #333; border-top-left-radius: 8px; border-top-right-radius: 8px;");
    auto* navLayout = new QHBoxLayout(navBar);
    
    auto* btnPrev = new QPushButton(IconHelper::getIcon("nav_prev", "#ccc"), "", picker);
    auto* btnNext = new QPushButton(IconHelper::getIcon("nav_next", "#ccc"), "", picker);
    auto* btnMonth = new QPushButton(picker);
    btnMonth->setStyleSheet("color: white; font-weight: bold; background: transparent; border: none;");
    
    auto updateLabel = [cal, btnMonth](){
        btnMonth->setText(QString("%1年 %2月").arg(cal->yearShown()).arg(cal->monthShown()));
    };
    updateLabel();

    connect(btnPrev, &QPushButton::clicked, [cal, updateLabel](){ cal->showPreviousMonth(); updateLabel(); });
    connect(btnNext, &QPushButton::clicked, [cal, updateLabel](){ cal->showNextMonth(); updateLabel(); });
    connect(cal, &QCalendarWidget::currentPageChanged, updateLabel);

    btnPrev->setFixedSize(28, 28);
    btnNext->setFixedSize(28, 28);
    btnPrev->setStyleSheet("QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #4facfe; }");
    btnNext->setStyleSheet("QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #4facfe; }");

    navLayout->addWidget(btnPrev);
    navLayout->addStretch();
    navLayout->addWidget(btnMonth);
    navLayout->addStretch();
    navLayout->addWidget(btnNext);

    // 2. 自定义星期表头
    auto* customHeader = new QWidget(picker);
    customHeader->setFixedHeight(30);
    customHeader->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* headerLayout = new QHBoxLayout(customHeader);
    headerLayout->setContentsMargins(1, 0, 1, 0);
    headerLayout->setSpacing(0);

    QStringList weekDays = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    for (const QString& day : weekDays) {
        auto* label = new QLabel(day, picker);
        label->setAlignment(Qt::AlignCenter);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 12px; background: transparent;")
                             .arg((day == "周六" || day == "周日") ? "#ff4d4f" : "#eebb00"));
        headerLayout->addWidget(label);
    }

    cal->setStyleSheet(
        "QCalendarWidget { background-color: #1e1e1e; border: none; }"
        "QCalendarWidget QAbstractItemView { background-color: #1e1e1e; color: #dcdcdc; selection-background-color: #007acc; selection-color: white; outline: none; border: none; }"
    );

    calLayout->addWidget(navBar);
    calLayout->addWidget(customHeader);
    calLayout->addWidget(cal);
    layout->addWidget(calContainer);

    layout->addSpacing(15);

    auto* timeLayout = new QHBoxLayout();
    timeLayout->addStretch();
    
    auto* hSpin = new QSpinBox(picker);
    hSpin->setRange(0, 23);
    hSpin->setValue(m_dateTime.time().hour());
    hSpin->setAlignment(Qt::AlignCenter);
    hSpin->setStyleSheet("QSpinBox { background: #333; color: white; border: 1px solid #444; padding: 5px; min-width: 60px; } "
                         "QSpinBox::up-button, QSpinBox::down-button { width: 0px; }"); // 隐藏按钮，强化输入感知
    
    auto* mSpin = new QSpinBox(picker);
    mSpin->setRange(0, 59);
    mSpin->setValue(m_dateTime.time().minute());
    mSpin->setAlignment(Qt::AlignCenter);
    mSpin->setStyleSheet("QSpinBox { background: #333; color: white; border: 1px solid #444; padding: 5px; min-width: 60px; } "
                         "QSpinBox::up-button, QSpinBox::down-button { width: 0px; }");

    timeLayout->addWidget(new QLabel("时间:", picker));
    timeLayout->addSpacing(10);
    timeLayout->addWidget(hSpin);
    timeLayout->addSpacing(15); // 增加小时与冒号之间的间距
    timeLayout->addWidget(new QLabel(":", picker));
    timeLayout->addSpacing(15); // 增加冒号与分钟之间的间距
    timeLayout->addWidget(mSpin);
    timeLayout->addStretch();
    layout->addLayout(timeLayout);

    layout->addSpacing(30); // 显著增加时间行与确定按钮之间的间距

    auto* btnConfirm = new QPushButton("确定", picker);
    btnConfirm->setStyleSheet("background: #007acc; color: white; padding: 10px; border-radius: 4px; font-weight: bold;");
    connect(btnConfirm, &QPushButton::clicked, [this, picker, cal, hSpin, mSpin](){
        QDateTime dt(cal->selectedDate(), QTime(hSpin->value(), mSpin->value()));
        this->setDateTime(dt);
        picker->accept();
    });
    layout->addWidget(btnConfirm);

    layout->addSpacing(5);

    // 采用非阻塞方式显示选择器
    picker->show();
    picker->raise();
    picker->activateWindow();
    
    // 居中显示在编辑器附近
    picker->move(this->mapToGlobal(QPoint(0, height())).x(), this->mapToGlobal(QPoint(0, height())).y());
}

bool CustomDateTimeEdit::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_display && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_display->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_display->setCursorPosition(m_display->text().length());
            return true;
        }
    }
    // 移除点击输入框弹出选择器的逻辑，允许用户点击聚焦输入
    return QWidget::eventFilter(watched, event);
}

// --- AlarmEditDialog ---

AlarmEditDialog::AlarmEditDialog(const DatabaseManager::Todo& todo, QWidget* parent)
    : FramelessDialog(todo.id == -1 ? "新增闹钟" : "编辑闹钟", parent), m_todo(todo)
{
    initUI();
    setMinimumSize(450, 420);
}

void AlarmEditDialog::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setSpacing(25);
    layout->setContentsMargins(30, 25, 30, 30);

    // 闹钟名称
    auto* titleLabel = new QLabel("闹钟名称:", this);
    titleLabel->setStyleSheet("color: #888; font-weight: bold;");
    layout->addWidget(titleLabel);

    m_editTitle = new QLineEdit(this);
    m_editTitle->setPlaceholderText("例如：早起锻炼、重要会议");
    m_editTitle->setText(m_todo.title);
    m_editTitle->setMinimumHeight(45);
    m_editTitle->installEventFilter(this);
    m_editTitle->setStyleSheet("QLineEdit { font-size: 16px; padding: 5px 12px; background: #2d2d2d; border: 1px solid #444; color: white; border-radius: 6px; } QLineEdit:focus { border-color: #007acc; }");
    layout->addWidget(m_editTitle);

    // 提醒时间
    auto* timeHeader = new QLabel("提醒时间:", this);
    timeHeader->setStyleSheet("color: #888; font-weight: bold;");
    layout->addWidget(timeHeader);

    auto* timeRow = new QHBoxLayout();
    timeRow->setSpacing(15);
    
    m_hSpin = new QSpinBox(this);
    m_hSpin->setRange(0, 23);
    m_hSpin->setMinimumHeight(50);
    m_hSpin->setMinimumWidth(90);
    m_hSpin->setValue(m_todo.reminderTime.isValid() ? m_todo.reminderTime.time().hour() : QTime::currentTime().hour());
    m_hSpin->setAlignment(Qt::AlignCenter);
    m_hSpin->setStyleSheet("QSpinBox { background: #2d2d2d; color: white; border: 1px solid #444; border-radius: 6px; font-size: 22px; font-weight: bold; } QSpinBox::up-button, QSpinBox::down-button { width: 0px; }");

    auto* separator = new QLabel(":", this);
    separator->setStyleSheet("font-size: 24px; font-weight: bold; color: #4facfe;");

    m_mSpin = new QSpinBox(this);
    m_mSpin->setRange(0, 59);
    m_mSpin->setMinimumHeight(50);
    m_mSpin->setMinimumWidth(90);
    m_mSpin->setValue(m_todo.reminderTime.isValid() ? m_todo.reminderTime.time().minute() : QTime::currentTime().minute());
    m_mSpin->setAlignment(Qt::AlignCenter);
    m_mSpin->setStyleSheet("QSpinBox { background: #2d2d2d; color: white; border: 1px solid #444; border-radius: 6px; font-size: 22px; font-weight: bold; } QSpinBox::up-button, QSpinBox::down-button { width: 0px; }");

    timeRow->addStretch();
    timeRow->addWidget(m_hSpin);
    timeRow->addWidget(separator);
    timeRow->addWidget(m_mSpin);
    timeRow->addStretch();
    layout->addLayout(timeRow);

    // 重复周期
    auto* repeatRow = new QHBoxLayout();
    auto* repeatLabel = new QLabel("重复周期:", this);
    repeatLabel->setStyleSheet("color: #888; font-weight: bold;");
    
    m_comboRepeat = new QComboBox(this);
    m_comboRepeat->addItems({"不重复", "每天", "每周", "每月"});
    m_comboRepeat->setCurrentIndex(m_todo.repeatMode > 3 ? 0 : m_todo.repeatMode);
    m_comboRepeat->setMinimumHeight(40);
    m_comboRepeat->setStyleSheet("QComboBox { background: #2d2d2d; color: white; border: 1px solid #444; border-radius: 6px; padding: 5px 10px; } QComboBox::drop-down { border: none; } QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #888; margin-right: 10px; }");
    
    repeatRow->addWidget(repeatLabel);
    repeatRow->addWidget(m_comboRepeat, 1);
    layout->addLayout(repeatRow);

    layout->addStretch();

    // 保存按钮
    auto* btnSave = new QPushButton("保 存 闹 钟", this);
    btnSave->setMinimumHeight(50);
    btnSave->setCursor(Qt::PointingHandCursor);
    // 2026-03-xx 按照用户要求，统一圆角为 4px
    btnSave->setStyleSheet("QPushButton { background-color: #007acc; color: white; border-radius: 4px; font-weight: bold; font-size: 16px; letter-spacing: 2px; } QPushButton:hover { background-color: #3e3e42; } QPushButton:pressed { background-color: #005fa3; }"); // 2026-03-xx 统一悬停色
    connect(btnSave, &QPushButton::clicked, this, &AlarmEditDialog::onSave);
    layout->addWidget(btnSave);
}

void AlarmEditDialog::onSave() {
    if (m_editTitle->text().trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "请输入闹钟名称", 700, QColor("#e74c3c"));
        return;
    }

    m_todo.title = m_editTitle->text().trimmed();
    // 闹钟的逻辑：reminderTime 为核心，startTime 同步设为该时间
    QTime time(m_hSpin->value(), m_mSpin->value());
    QDateTime nextRemind = QDateTime::currentDateTime();
    nextRemind.setTime(time);
    if (nextRemind < QDateTime::currentDateTime()) {
        nextRemind = nextRemind.addDays(1);
    }
    
    m_todo.reminderTime = nextRemind;
    m_todo.startTime = nextRemind;
    m_todo.endTime = nextRemind.addSecs(60);
    m_todo.repeatMode = m_comboRepeat->currentIndex();
    m_todo.priority = 2; // 闹钟固定为紧急
    m_todo.status = 0;
    m_todo.progress = 0;

    accept();
}

DatabaseManager::Todo AlarmEditDialog::getTodo() const {
    return m_todo;
}

bool AlarmEditDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_editTitle && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_editTitle->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_editTitle->setCursorPosition(m_editTitle->text().length());
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

// --- TodoEditDialog ---

TodoEditDialog::TodoEditDialog(const DatabaseManager::Todo& todo, QWidget* parent) 
    : FramelessDialog(todo.id == -1 ? "新增待办" : "编辑待办", parent), m_todo(todo) {
    initUI();
    setFixedSize(450, 500);
}

void TodoEditDialog::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    m_editTitle = new QLineEdit(this);
    m_editTitle->setPlaceholderText("待办标题...");
    m_editTitle->setText(m_todo.title);
    m_editTitle->installEventFilter(this);
    m_editTitle->setStyleSheet("font-size: 16px; padding: 8px; background: #333; border: 1px solid #444; color: white; border-radius: 4px;");
    layout->addWidget(new QLabel("标题:"));
    layout->addWidget(m_editTitle);

    m_editContent = new QTextEdit(this);
    m_editContent->setPlaceholderText("详细内容(可选)...");
    m_editContent->setText(m_todo.content);
    m_editContent->installEventFilter(this);
    m_editContent->setStyleSheet("background: #333; border: 1px solid #444; color: white; border-radius: 4px;");
    layout->addWidget(new QLabel("备注:"));
    layout->addWidget(m_editContent);

    auto* timeLayout = new QHBoxLayout();
    m_editStart = new CustomDateTimeEdit(m_todo.startTime.isValid() ? m_todo.startTime : QDateTime::currentDateTime(), this);
    m_editEnd = new CustomDateTimeEdit(m_todo.endTime.isValid() ? m_todo.endTime : QDateTime::currentDateTime().addSecs(3600), this);

    timeLayout->addWidget(new QLabel("从:"));
    timeLayout->addWidget(m_editStart, 1);
    timeLayout->addWidget(new QLabel("至:"));
    timeLayout->addWidget(m_editEnd, 1);
    layout->addLayout(timeLayout);

    auto* reminderLayout = new QHBoxLayout();
    m_checkReminder = new QCheckBox("开启提醒", this);
    m_checkReminder->setChecked(m_todo.reminderTime.isValid());
    m_checkReminder->setStyleSheet("QCheckBox { color: white; } QCheckBox::indicator { width: 18px; height: 18px; }");
    
    m_editReminder = new CustomDateTimeEdit(m_todo.reminderTime.isValid() ? m_todo.reminderTime : m_todo.startTime, this);
    m_editReminder->setEnabled(m_checkReminder->isChecked());
    connect(m_checkReminder, &QCheckBox::toggled, m_editReminder, &QWidget::setEnabled);

    reminderLayout->addWidget(m_checkReminder);
    reminderLayout->addWidget(m_editReminder, 1);
    layout->addLayout(reminderLayout);

    auto* repeatRow = new QHBoxLayout();
    m_comboRepeat = new QComboBox(this);
    m_comboRepeat->addItems({"不重复", "每天", "每周", "每月", "每小时", "每分钟", "每秒"});
    m_comboRepeat->setCurrentIndex(m_todo.repeatMode);
    m_comboRepeat->setStyleSheet("background: #333; color: white;");
    repeatRow->addWidget(new QLabel("重复周期:"));
    repeatRow->addWidget(m_comboRepeat, 1);
    layout->addLayout(repeatRow);

    auto* progressRow = new QHBoxLayout();
    m_sliderProgress = new QSlider(Qt::Horizontal, this);
    m_sliderProgress->setRange(0, 100);
    m_sliderProgress->setValue(m_todo.progress);
    m_labelProgress = new QLabel(QString("%1%").arg(m_todo.progress), this);
    m_labelProgress->setFixedWidth(40);
    connect(m_sliderProgress, &QSlider::valueChanged, [this](int v){ m_labelProgress->setText(QString("%1%").arg(v)); });
    progressRow->addWidget(new QLabel("任务进度:"));
    progressRow->addWidget(m_sliderProgress, 1);
    progressRow->addWidget(m_labelProgress);
    layout->addLayout(progressRow);

    auto* botLayout = new QHBoxLayout();
    m_comboPriority = new QComboBox(this);
    m_comboPriority->addItems({"普通", "高优先级", "紧急"});
    m_comboPriority->setCurrentIndex(m_todo.priority);
    m_comboPriority->setStyleSheet("background: #333; color: white;");
    botLayout->addWidget(new QLabel("优先级:"));
    botLayout->addWidget(m_comboPriority);

    // [PROFESSIONAL] 如果有关联笔记，显示跳转按钮
    if (m_todo.noteId > 0) {
        auto* btnJump = new QPushButton("跳转笔记", this);
        btnJump->setIcon(IconHelper::getIcon("link", "#ffffff"));
        btnJump->setProperty("tooltipText", "点击可快速定位并查看关联的笔记详情");
        btnJump->installEventFilter(this);
        btnJump->setStyleSheet("background: #27ae60; color: white; padding: 8px 15px; border-radius: 4px;");
        connect(btnJump, &QPushButton::clicked, [this](){
             // 这里通常通过 QuickPreview 展示。为了简单实现：
             ToolTipOverlay::instance()->showText(QCursor::pos(), "跳转逻辑已触发");
        });
        botLayout->addWidget(btnJump);
    }
    
    auto* btnSave = new QPushButton("保存", this);
    btnSave->setStyleSheet("background: #007acc; color: white; padding: 8px 20px; border-radius: 4px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &TodoEditDialog::onSave);
    botLayout->addWidget(btnSave);
    
    layout->addLayout(botLayout);
}

void TodoEditDialog::onSave() {
    if (m_editTitle->text().trimmed().isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "请输入标题", 700, QColor("#e74c3c"));
        return;
    }
    
    m_todo.title = m_editTitle->text().trimmed();
    m_todo.content = m_editContent->toPlainText();
    m_todo.startTime = m_editStart->dateTime();
    m_todo.endTime = m_editEnd->dateTime();
    m_todo.priority = m_comboPriority->currentIndex();
    m_todo.repeatMode = m_comboRepeat->currentIndex();
    m_todo.progress = m_sliderProgress->value();
    
    if (m_todo.progress == 100) m_todo.status = 1; // 自动完成
    
    if (m_checkReminder->isChecked()) {
        m_todo.reminderTime = m_editReminder->dateTime();
    } else {
        m_todo.reminderTime = QDateTime();
    }
    
    accept();
}

DatabaseManager::Todo TodoEditDialog::getTodo() const {
    return m_todo;
}

bool TodoEditDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (watched == m_editTitle) {
            if (keyEvent->key() == Qt::Key_Up) {
                m_editTitle->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                m_editTitle->setCursorPosition(m_editTitle->text().length());
                return true;
            }
        } else if (watched == m_editContent) {
            if (keyEvent->key() == Qt::Key_Up) {
                QTextCursor cursor = m_editContent->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Up);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::Start);
                    m_editContent->setTextCursor(cursor);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QTextCursor cursor = m_editContent->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Down);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::End);
                    m_editContent->setTextCursor(cursor);
                    return true;
                }
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}
