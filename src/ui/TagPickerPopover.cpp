#include "TagPickerPopover.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/TagRepository.h"
#include "UiHelper.h"
#include "../core/AppConfig.h"
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QApplication>
#include <QStyleOption>
#include <QGuiApplication>
#include <QInputMethod>
#include <QFrame>

namespace ArcMeta {

// 初始化会话级最近使用队列静态成员变量
QStringList TagPickerPopover::s_sessionRecentTags;

// ============================================================================
// TagItemButton 实现
// ============================================================================
TagItemButton::TagItemButton(const QString& name, int count, IconType iconType, QWidget* parent)
    : QPushButton(parent), m_tagName(name), m_count(count), m_iconType(iconType) 
{
    setFixedHeight(26);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    // 1. 左侧 SVG 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QIcon icon;
    if (m_iconType == Clock) {
        icon = UiHelper::getIcon("clock", QColor("#888888"), 14);
    } else {
        icon = UiHelper::getIcon("circle_filled", QColor("#888888"), 10);
    }
    m_iconLabel->setPixmap(icon.pixmap(14, 14));
    layout->addWidget(m_iconLabel);

    // 2. 富文本标签
    m_textLabel = new QLabel(this);
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_textLabel, 1);

    setSelected(false);
}

void TagItemButton::setSelected(bool selected) {
    m_selected = selected;
    
    QString nameColor = m_selected ? "#FFFFFF" : "#EEEEEE";
    QString countColor = m_selected ? "#D4D4D4" : "#888888";
    
    QString displayText = QString("<span style='color:%1; font-weight:bold; font-size:12px;'>%2</span>").arg(nameColor).arg(m_tagName);
    if (m_count > 0) {
        displayText += QString(" <span style='color:%1; font-size:11px;'>(%2)</span>").arg(countColor).arg(m_count);
    }
    m_textLabel->setText(displayText);
    
    update();
}

void TagItemButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    
    QColor bg, border;
    if (m_selected) {
        bg = QColor("#3498db");      // 品牌选中蓝
        border = QColor("#5dade2");  // 选中高亮边
    } else {
        bg = QColor("transparent");
        border = QColor("transparent");
    }

    painter.setBrush(bg);
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(r, 4, 4); // 规范圆角 4px
}


// ============================================================================
// TagPickerPopover 实现
// ============================================================================
TagPickerPopover::TagPickerPopover(QWidget* parent)
    : QWidget(parent) // 🚨 废除 Qt::Popup，改用同窗口 Child Overlay 架构以保障 Windows IME 正常挂载与上下文畅通
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    // 读取折叠/展开习惯
    bool sidebarVisible = AppConfig::instance().getValue("TagPicker/SidebarVisible", false).toBool();
    resize(sidebarVisible ? 400 : 200, 260); // 动态初始化尺寸

    // 主布局
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(1, 1, 1, 1);
    mainLayout->setSpacing(0);

    // 容器组件，用于提供背景边框与圆角
    auto* container = new QWidget(this);
    container->setObjectName("PopoverContainer");
    container->setStyleSheet(
        "QWidget#PopoverContainer {"
        "  background-color: #1E1E1E;"
        "  border: 1px solid #3C3C3C;"
        "  border-radius: 8px;"
        "}"
    );
    mainLayout->addWidget(container);

    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(10, 10, 10, 10);
    containerLayout->setSpacing(8);

    // 1. 顶部搜索框
    m_searchEdit = new QLineEdit(container);
    m_searchEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_searchEdit->setInputMethodHints(Qt::ImhPreferLowercase | Qt::ImhNoAutoUppercase);
    m_searchEdit->setPlaceholderText("搜索...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumHeight(30);
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #444444;"
        "  border-radius: 4px;"
        "  padding: 0px 8px 0px 2px;" // 修正：左边距缩减至 2px，紧贴 SVG 图标
        "  color: #FFFFFF;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3498db; }"
    );

    // 1:1 矢量图标配置
    m_searchEdit->addAction(UiHelper::getIcon("search", QColor("#888888"), 14), QLineEdit::LeadingPosition);
    auto* sidebarAction = m_searchEdit->addAction(UiHelper::getIcon("sidebar", QColor("#888888"), 14), QLineEdit::TrailingPosition);
    connect(sidebarAction, &QAction::triggered, this, &TagPickerPopover::toggleSidebar);
    m_searchEdit->addAction(UiHelper::getIcon("filter_funnel_outline", QColor("#888888"), 14), QLineEdit::TrailingPosition);

    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagPickerPopover::onSearchTextChanged);
    containerLayout->addWidget(m_searchEdit);

    // 2. 双栏布局核心装载
    m_columnsLayout = new QHBoxLayout();
    m_columnsLayout->setContentsMargins(0, 0, 0, 0);
    m_columnsLayout->setSpacing(0);

    // 2.1 左侧栏：标签组侧边栏 (宽120px)
    m_sidebarWidget = new QWidget(container);
    m_sidebarWidget->setFixedWidth(120);
    m_sidebarWidget->setStyleSheet("background: transparent;");
    auto* sidebarL = new QVBoxLayout(m_sidebarWidget);
    sidebarL->setContentsMargins(0, 0, 0, 0);
    sidebarL->setSpacing(0);

    m_sidebarScroll = new QScrollArea(m_sidebarWidget);
    m_sidebarScroll->setWidgetResizable(true);
    m_sidebarScroll->setFrameShape(QFrame::NoFrame);
    m_sidebarScroll->setStyleSheet("QScrollArea { background: transparent; }");
    m_sidebarScroll->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: #333; border-radius: 2px; }"
    );

    m_sidebarContainer = new QWidget(m_sidebarScroll);
    m_sidebarContainer->setStyleSheet("background: transparent;");
    m_sidebarLayout = new QVBoxLayout(m_sidebarContainer);
    m_sidebarLayout->setContentsMargins(0, 0, 4, 0);
    m_sidebarLayout->setSpacing(4);
    m_sidebarLayout->addStretch(1);
    
    m_sidebarScroll->setWidget(m_sidebarContainer);
    sidebarL->addWidget(m_sidebarScroll, 1);
    m_columnsLayout->addWidget(m_sidebarWidget);

    // 2.2 中间 1px 分割线 (#333333)
    m_dividerLine = new QFrame(container);
    m_dividerLine->setFrameShape(QFrame::VLine);
    m_dividerLine->setFrameShadow(QFrame::Plain);
    m_dividerLine->setFixedWidth(1);
    m_dividerLine->setStyleSheet("background-color: #333333; border: none;");
    m_columnsLayout->addWidget(m_dividerLine);

    // 2.3 右侧栏：滚动列表区
    m_scrollArea = new QScrollArea(container);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; }");
    m_scrollArea->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #666; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    m_scrollContainer = new QWidget(m_scrollArea);
    m_scrollContainer->setStyleSheet("background: transparent;");
    m_scrollLayout = new QVBoxLayout(m_scrollContainer);
    m_scrollLayout->setContentsMargins(8, 0, 4, 0);
    m_scrollLayout->setSpacing(12);

    // 最近使用分组
    m_recentGroup = new QWidget(m_scrollContainer);
    auto* recentLayout = new QVBoxLayout(m_recentGroup);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(6);

    m_recentLabel = new QLabel(m_recentGroup);
    m_recentLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    recentLayout->addWidget(m_recentLabel);

    auto* recentLine = new QFrame(m_recentGroup);
    recentLine->setFixedHeight(1);
    recentLine->setStyleSheet("background-color: #333333; border: none;");
    recentLayout->addWidget(recentLine);

    m_recentContainer = new QWidget(m_recentGroup);
    m_recentGrid = new QGridLayout(m_recentContainer);
    m_recentGrid->setContentsMargins(0, 2, 0, 2);
    m_recentGrid->setSpacing(6);
    m_recentContainer->setLayout(m_recentGrid);
    recentLayout->addWidget(m_recentContainer);
    m_scrollLayout->addWidget(m_recentGroup);

    // 其它分组
    m_globalGroup = new QWidget(m_scrollContainer);
    auto* globalLayout = new QVBoxLayout(m_globalGroup);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(6);

    m_globalLabel = new QLabel(m_globalGroup);
    m_globalLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    globalLayout->addWidget(m_globalLabel);

    auto* globalLine = new QFrame(m_globalGroup);
    globalLine->setFixedHeight(1);
    globalLine->setStyleSheet("background-color: #333333; border: none;");
    globalLayout->addWidget(globalLine);

    m_globalContainer = new QWidget(m_globalGroup);
    m_globalGrid = new QGridLayout(m_globalContainer);
    m_globalGrid->setContentsMargins(0, 2, 0, 2);
    m_globalGrid->setSpacing(6);
    m_globalContainer->setLayout(m_globalGrid);
    globalLayout->addWidget(m_globalContainer);
    m_scrollLayout->addWidget(m_globalGroup);

    m_scrollLayout->addStretch(1);
    m_scrollArea->setWidget(m_scrollContainer);
    m_columnsLayout->addWidget(m_scrollArea);
    
    containerLayout->addLayout(m_columnsLayout, 1);

    // 3. 底部提示条
    m_hintLabel = new QLabel("切换 Tab    移动 ↑↓←→    选中 ↵    关闭 ESC", container);
    m_hintLabel->setStyleSheet("color: #666666; font-size: 11px;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(m_hintLabel);

    setSidebarVisible(sidebarVisible);
}

TagPickerPopover::~TagPickerPopover() {
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void TagPickerPopover::toggleSidebar() {
    bool isVisible = m_sidebarWidget->isVisible();
    setSidebarVisible(!isVisible);
}

void TagPickerPopover::setSidebarVisible(bool visible) {
    m_sidebarWidget->setVisible(visible);
    m_dividerLine->setVisible(visible);
    
    if (visible) {
        setMinimumWidth(400);
        setMaximumWidth(16777215);
        resize(400, height());
    } else {
        setMinimumWidth(200);
        setMaximumWidth(16777215);
        resize(200, height());
    }
    
    AppConfig::instance().setValue("TagPicker/SidebarVisible", visible);
    AppConfig::instance().sync();
}

void TagPickerPopover::showAt(const QPoint& globalPos) {
    m_allTags = CategoryRepo::getGlobalUniqueTags();

    // 1. 初始化 s_sessionRecentTags
    if (s_sessionRecentTags.isEmpty()) {
        QList<QPair<QString, int>> tempSorted;
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
            tempSorted.append({it.key(), it.value()});
        }
        std::sort(tempSorted.begin(), tempSorted.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return a.second > b.second;
        });
        for (int i = 0; i < qMin(6, tempSorted.size()); ++i) {
            s_sessionRecentTags.append(tempSorted[i].first);
        }
    }

    // 2. 根据频次对 top tags 排序展示
    m_topTags.clear();
    for (const auto& tagName : s_sessionRecentTags) {
        if (m_allTags.contains(tagName)) {
            m_topTags.append({tagName, m_allTags[tagName]});
        }
    }

    m_searchEdit->clear();
    m_focusZone = SearchBar;

    refreshSidebar();
    refreshList();
    
    if (parentWidget()) {
        QPoint localPos = parentWidget()->mapFromGlobal(globalPos);
        int x = localPos.x();
        int y = localPos.y();
        
        if (x + width() > parentWidget()->width()) {
            x = parentWidget()->width() - width() - 10;
        }
        if (y + height() > parentWidget()->height()) {
            y = parentWidget()->height() - height() - 10;
        }
        if (x < 10) x = 10;
        if (y < 10) y = 10;
        
        move(x, y);
    }
    
    show();
    raise(); 
    m_searchEdit->setFocus();

    if (qApp) {
        qApp->removeEventFilter(this);
        qApp->installEventFilter(this);
    }
}

void TagPickerPopover::recordTagUsage(const QString& tagName) {
    QString cleaned = tagName.trimmed();
    if (cleaned.isEmpty()) return;
    
    s_sessionRecentTags.removeAll(cleaned);
    s_sessionRecentTags.prepend(cleaned);
    if (s_sessionRecentTags.size() > 10) {
        s_sessionRecentTags.removeLast();
    }
}

void TagPickerPopover::refreshSidebar() {
    qDeleteAll(m_sidebarButtons);
    m_sidebarButtons.clear();
    m_selectedSidebarIndex = 0;

    for (int i = m_sidebarLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_sidebarLayout->itemAt(i);
        if (item->widget()) {
            item->widget()->deleteLater();
            m_sidebarLayout->removeItem(item);
            delete item;
        }
    }

    QSet<QString> groupedTags;
    auto allGroups = TagRepository::getAllGroups();
    for (const auto& g : allGroups) {
        for (const auto& t : g.tags) {
            groupedTags.insert(t.trimmed());
        }
    }
    int uncategorizedCount = 0;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        if (!groupedTags.contains(it.key())) {
            uncategorizedCount++;
        }
    }

    auto createSidebarBtn = [this](const QString& label, int count, int groupId) {
        QPushButton* btn = new QPushButton(m_sidebarContainer);
        btn->setProperty("groupId", groupId);
        btn->setText(QString("%1 (%2)").arg(label).arg(count));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { text-align: left; background: transparent; border: none; color: #AAAAAA; padding: 6px 8px; font-size: 11px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #2D2D2D; color: #EEEEEE; }"
        );
        m_sidebarLayout->insertWidget(m_sidebarLayout->count() - 1, btn);
        m_sidebarButtons.append(btn);

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            int idx = m_sidebarButtons.indexOf(btn);
            if (idx >= 0) {
                m_selectedSidebarIndex = idx;
                m_focusZone = SidebarZone;
                updateSidebarHighlight();
            }
        });
    };

    createSidebarBtn("全部", m_allTags.size(), -1);
    createSidebarBtn("未分类", uncategorizedCount, -2);

    for (const auto& g : allGroups) {
        createSidebarBtn(g.name, g.tags.size(), g.id);
    }

    updateSidebarHighlight();
}

void TagPickerPopover::updateSidebarHighlight() {
    if (m_selectedSidebarIndex < 0 || m_selectedSidebarIndex >= m_sidebarButtons.size()) {
        return;
    }

    m_selectedGroupId = m_sidebarButtons[m_selectedSidebarIndex]->property("groupId").toInt();

    for (int i = 0; i < m_sidebarButtons.size(); ++i) {
        QPushButton* btn = m_sidebarButtons[i];
        bool isActive = (i == m_selectedSidebarIndex);
        if (isActive) {
            btn->setStyleSheet(
                "QPushButton { text-align: left; background-color: #2A2A2A; border: none; color: #1abc9c; padding: 6px 8px; font-weight: bold; font-size: 11px; border-radius: 4px; }"
            );
            m_sidebarScroll->ensureWidgetVisible(btn);
        } else {
            btn->setStyleSheet(
                "QPushButton { text-align: left; background: transparent; border: none; color: #AAAAAA; padding: 6px 8px; font-size: 11px; border-radius: 4px; }"
                "QPushButton:hover { background-color: #2D2D2D; color: #EEEEEE; }"
            );
        }
    }

    if (m_focusZone == SidebarZone) {
        m_sidebarWidget->setStyleSheet("QWidget { border: 1px solid #1abc9c; border-radius: 4px; }");
    } else {
        m_sidebarWidget->setStyleSheet("QWidget { border: none; }");
    }

    refreshList();
}

void TagPickerPopover::refreshList() {
    qDeleteAll(m_visibleButtons);
    m_visibleButtons.clear();
    m_selectedIndex = -1;

    QLayoutItem* item;
    while ((item = m_recentGrid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while ((item = m_globalGrid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QString searchFilter = m_searchEdit->text().trimmed().toLower();

    QSet<QString> activeTags;
    QSet<QString> groupedTags;
    auto allGroups = TagRepository::getAllGroups();
    for (const auto& g : allGroups) {
        for (const auto& t : g.tags) groupedTags.insert(t.trimmed());
    }

    if (m_selectedGroupId == -1) {
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) activeTags.insert(it.key());
    } else if (m_selectedGroupId == -2) {
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
            if (!groupedTags.contains(it.key())) activeTags.insert(it.key());
        }
    } else {
        for (const auto& g : allGroups) {
            if (g.id == m_selectedGroupId) {
                for (const auto& t : g.tags) {
                    QString tagStr = t.trimmed();
                    if (!tagStr.isEmpty() && m_allTags.contains(tagStr)) activeTags.insert(tagStr);
                }
                break;
            }
        }
    }

    QSet<QString> displayedRecentTags;

    // 渲染最近使用网格 (秒级置顶刷新)
    int recentCount = 0;
    for (const auto& pair : m_topTags) {
        QString tagName = pair.first;
        int count = pair.second;
        
        if (!activeTags.contains(tagName)) continue;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) continue;
        
        auto* btn = new TagItemButton(tagName, count, TagItemButton::Clock, m_recentContainer);
        m_recentGrid->addWidget(btn, recentCount / 2, recentCount % 2);
        m_visibleButtons.append(btn);
        displayedRecentTags.insert(tagName);
        recentCount++;

        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
            recordTagUsage(tagName);
            hide();
            if (qApp) qApp->removeEventFilter(this);
        });
        btn->installEventFilter(this);
    }

    // 渲染其它网格
    int globalCount = 0;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        QString tagName = it.key();
        int count = it.value();
        
        if (!activeTags.contains(tagName)) continue;
        if (displayedRecentTags.contains(tagName)) continue;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) continue;

        auto* btn = new TagItemButton(tagName, count, TagItemButton::CircleFilled, m_globalContainer);
        m_globalGrid->addWidget(btn, globalCount / 2, globalCount % 2);
        m_visibleButtons.append(btn);
        globalCount++;

        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
            recordTagUsage(tagName);
            hide();
            if (qApp) qApp->removeEventFilter(this);
        });
        btn->installEventFilter(this);
    }

    m_recentLabel->setText(QString("最近使用 (%1)").arg(recentCount));
    m_recentGroup->setVisible(recentCount > 0);

    m_globalLabel->setText(QString("其它 (%1)").arg(globalCount));
    m_globalGroup->setVisible(globalCount > 0);

    if (!m_visibleButtons.isEmpty()) {
        m_selectedIndex = 0;
        updateSelectionHighlight();
    }
}

void TagPickerPopover::onSearchTextChanged(const QString&) {
    refreshList();
}

void TagPickerPopover::updateSelectionHighlight() {
    for (int i = 0; i < m_visibleButtons.size(); ++i) {
        bool shouldHighlight = (m_focusZone == GridZone) ? (i == m_selectedIndex) : false;
        m_visibleButtons[i]->setSelected(shouldHighlight);
    }

    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        auto* btn = m_visibleButtons[m_selectedIndex];
        m_scrollArea->ensureWidgetVisible(btn);
    }
}

void TagPickerPopover::selectCurrent() {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        QString tagName = m_visibleButtons[m_selectedIndex]->tagName();
        emit tagSelected(tagName);
        recordTagUsage(tagName);
    } else {
        QString searchFilter = m_searchEdit->text().trimmed();
        if (!searchFilter.isEmpty()) {
            emit tagSelected(searchFilter);
            recordTagUsage(searchFilter);
            m_searchEdit->clear();
        }
    }
    hide();
    if (qApp) qApp->removeEventFilter(this);
}

void TagPickerPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        if (qApp) qApp->removeEventFilter(this);
        event->accept();
        return;
    }

    // Tab 焦点轮转
    if (event->key() == Qt::Key_Tab) {
        if (m_focusZone == SearchBar) {
            if (m_sidebarWidget->isVisible()) {
                m_focusZone = SidebarZone;
                m_searchEdit->clearFocus();
                updateSidebarHighlight();
                updateSelectionHighlight();
            } else {
                m_focusZone = GridZone;
                m_searchEdit->clearFocus();
                updateSelectionHighlight();
            }
        } else if (m_focusZone == SidebarZone) {
            m_focusZone = GridZone;
            m_sidebarWidget->setStyleSheet("QWidget { border: none; }");
            updateSelectionHighlight();
        } else {
            m_focusZone = SearchBar;
            m_searchEdit->setFocus();
            updateSelectionHighlight();
        }
        event->accept();
        return;
    }

    // 左栏操作
    if (m_focusZone == SidebarZone) {
        int count = m_sidebarButtons.size();
        if (count > 0) {
            if (event->key() == Qt::Key_Down) {
                m_selectedSidebarIndex = (m_selectedSidebarIndex + 1) % count;
                updateSidebarHighlight();
                event->accept();
                return;
            } else if (event->key() == Qt::Key_Up) {
                m_selectedSidebarIndex = (m_selectedSidebarIndex - 1 + count) % count;
                updateSidebarHighlight();
                event->accept();
                return;
            } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                m_focusZone = GridZone;
                updateSidebarHighlight();
                updateSelectionHighlight();
                event->accept();
                return;
            }
        }
    }

    // 右栏操作
    int btnCount = m_visibleButtons.size();
    if (btnCount > 0) {
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Right) {
            m_selectedIndex = (m_selectedIndex + 1) % btnCount;
            updateSelectionHighlight();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Left) {
            m_selectedIndex = (m_selectedIndex - 1 + btnCount) % btnCount;
            updateSelectionHighlight();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            selectCurrent();
            event->accept();
            return;
        }
    } else {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QString searchFilter = m_searchEdit->text().trimmed();
            if (!searchFilter.isEmpty()) {
                emit tagSelected(searchFilter);
                recordTagUsage(searchFilter);
                m_searchEdit->clear();
                hide();
                if (qApp) qApp->removeEventFilter(this);
                event->accept();
                return;
            }
        }
    }

    QWidget::keyPressEvent(event);
}

void TagPickerPopover::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

void TagPickerPopover::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    m_searchEdit->setFocus();
}

bool TagPickerPopover::eventFilter(QObject* watched, QEvent* event) {
    // 全局鼠标点击：点击 Popover 外部时自动淡隐关闭
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QPoint localPos = mapFromGlobal(globalPos);
        if (!rect().contains(localPos)) {
            hide();
            if (qApp) {
                qApp->removeEventFilter(this);
            }
            return false; // 放行点击，让外部响应
        }
    }

    // 按键拦截
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (QGuiApplication::inputMethod()->isVisible()) {
            return false; // 处于合成词确认阶段，直接放行上屏
        }

        int key = keyEvent->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up ||
            key == Qt::Key_Return || key == Qt::Key_Enter ||
            key == Qt::Key_Escape || key == Qt::Key_Tab) 
        {
            this->keyPressEvent(keyEvent);
            return true;
        }
    }

    // Hover
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) {
        auto* btn = qobject_cast<TagItemButton*>(watched);
        if (btn) {
            int idx = m_visibleButtons.indexOf(btn);
            if (idx >= 0 && idx != m_selectedIndex) {
                m_selectedIndex = idx;
                m_focusZone = GridZone;
                updateSelectionHighlight();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ArcMeta
