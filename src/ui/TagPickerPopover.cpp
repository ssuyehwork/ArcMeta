#include "TagPickerPopover.h"
#include "components/FlowLayout.h"
#include "../meta/MetadataManager.h"
#include "UiHelper.h"
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QApplication>
#include <QStyleOption>

namespace ArcMeta {

// ============================================================================
// TagItemButton 实现
// ============================================================================
TagItemButton::TagItemButton(const QString& name, int count, QWidget* parent)
    : QPushButton(parent), m_tagName(name), m_count(count)
{
    setFixedHeight(24);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QString displayText = m_tagName;
    if (m_count > 0) {
        displayText += QString(" (%1)").arg(m_count);
    }
    setText(displayText);

    // 计算宽度
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(displayText);
    setFixedWidth(textWidth + 20); // 左右各 10px 间距
}

void TagItemButton::setSelected(bool selected) {
    if (m_selected != selected) {
        m_selected = selected;
        update();
    }
}

void TagItemButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);

    QColor bg, border, textCol;
    if (m_selected) {
        bg = QColor("#3498db");      // 选中蓝
        border = QColor("#5dade2");  // 选中亮蓝边框
        textCol = QColor("#FFFFFF");
    } else {
        bg = QColor("#2B2B2B");      // 默认深灰
        border = QColor("#3C3C3C");  // 默认边框
        textCol = QColor("#EEEEEE");
    }

    painter.setBrush(bg);
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(r, 12, 12);

    painter.setPen(textCol);
    painter.setFont(font());
    painter.drawText(r, Qt::AlignCenter, text());
}


// ============================================================================
// TagPickerPopover 实现
// ============================================================================
TagPickerPopover::TagPickerPopover(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    resize(320, 360);

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
    m_searchEdit->setPlaceholderText("搜索...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumHeight(30);
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #444444;"
        "  border-radius: 4px;"
        "  padding: 0px 8px 0px 2px;" // 修正：将左 Padding 从 24px 缩减至 2px，消除文本脱节缺口
        "  color: #FFFFFF;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3498db; }"
    );

    // 添加放大镜图标
    QAction* searchIconAction = m_searchEdit->addAction(UiHelper::getIcon("search", QColor("#888888"), 14), QLineEdit::LeadingPosition);
    Q_UNUSED(searchIconAction);

    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagPickerPopover::onSearchTextChanged);
    containerLayout->addWidget(m_searchEdit);

    // 2. 滚动区
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
    m_scrollLayout->setContentsMargins(0, 0, 4, 0);
    m_scrollLayout->setSpacing(12);

    // 最近使用分组
    m_recentGroup = new QWidget(m_scrollContainer);
    auto* recentLayout = new QVBoxLayout(m_recentGroup);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(6);

    m_recentLabel = new QLabel("最近使用", m_recentGroup);
    m_recentLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    recentLayout->addWidget(m_recentLabel);

    m_recentContainer = new QWidget(m_recentGroup);
    m_recentFlow = new FlowLayout(m_recentContainer, 0, 6, 6);
    m_recentContainer->setLayout(m_recentFlow);
    recentLayout->addWidget(m_recentContainer);
    m_scrollLayout->addWidget(m_recentGroup);

    // 其它 / 全局分组
    m_globalGroup = new QWidget(m_scrollContainer);
    auto* globalLayout = new QVBoxLayout(m_globalGroup);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(6);

    m_globalLabel = new QLabel("其它 / 全局标签库", m_globalGroup);
    m_globalLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    globalLayout->addWidget(m_globalLabel);

    m_globalContainer = new QWidget(m_globalGroup);
    m_globalFlow = new FlowLayout(m_globalContainer, 0, 6, 6);
    m_globalContainer->setLayout(m_globalFlow);
    globalLayout->addWidget(m_globalContainer);
    m_scrollLayout->addWidget(m_globalGroup);

    m_scrollLayout->addStretch(1);
    m_scrollArea->setWidget(m_scrollContainer);
    containerLayout->addWidget(m_scrollArea, 1);

    // 3. 底部提示条
    m_hintLabel = new QLabel("移动 ↑↓←→  选中 ↵  关闭 ESC", container);
    m_hintLabel->setStyleSheet("color: #666666; font-size: 11px;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(m_hintLabel);
}

TagPickerPopover::~TagPickerPopover() {
}

void TagPickerPopover::showAt(const QPoint& globalPos) {
    // 每次打开时，重新从数据源读取所有 tags 与 top tags 并渲染
    m_allTags = MetadataManager::instance().getAllTags();
    m_topTags = MetadataManager::instance().getTopTags(10);

    m_searchEdit->clear();
    refreshList();

    move(globalPos);
    show();
    m_searchEdit->setFocus();
}

void TagPickerPopover::refreshList() {
    // 1. 清空现有的所有 button
    qDeleteAll(m_visibleButtons);
    m_visibleButtons.clear();
    m_selectedIndex = -1;

    // 清空 FlowLayout 中的旧 items
    while (m_recentFlow->count() > 0) {
        auto* item = m_recentFlow->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    while (m_globalFlow->count() > 0) {
        auto* item = m_globalFlow->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QString searchFilter = m_searchEdit->text().trimmed().toLower();

    // 记录已经添加到“最近”分组的标签名，避免“全局”分组中重复展示
    QSet<QString> displayedRecentTags;

    // 2. 渲染“最近使用”分组
    for (const auto& pair : m_topTags) {
        QString tagName = pair.first;
        int count = pair.second;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) {
            continue;
        }

        auto* btn = new TagItemButton(tagName, count, m_recentContainer);
        m_recentFlow->addWidget(btn);
        m_visibleButtons.append(btn);
        displayedRecentTags.insert(tagName);

        // 点击事件
        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
        });

        // 鼠标 Hover 同步选中状态
        btn->installEventFilter(this);
    }

    // 3. 渲染“其它 / 全局标签库”分组
    // getAllTags 返回 QMap，天生按 key 排序
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        QString tagName = it.key();
        int count = it.value();

        // 如果已经被最近使用分组展示了，则在全局中跳过
        if (displayedRecentTags.contains(tagName)) {
            continue;
        }

        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) {
            continue;
        }

        auto* btn = new TagItemButton(tagName, count, m_globalContainer);
        m_globalFlow->addWidget(btn);
        m_visibleButtons.append(btn);

        // 点击事件
        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
        });

        // 鼠标 Hover 同步选中状态
        btn->installEventFilter(this);
    }

    // 4. 显示与隐藏空的分组
    bool hasRecent = m_recentFlow->count() > 0;
    m_recentGroup->setVisible(hasRecent);

    bool hasGlobal = m_globalFlow->count() > 0;
    m_globalGroup->setVisible(hasGlobal);

    // 5. 初始化默认选中
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
        m_visibleButtons[i]->setSelected(i == m_selectedIndex);
    }

    // 滚动区域自适应可视滚动，确保当前选中的按钮可见
    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        auto* btn = m_visibleButtons[m_selectedIndex];
        m_scrollArea->ensureWidgetVisible(btn);
    }
}

void TagPickerPopover::selectCurrent() {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        QString tagName = m_visibleButtons[m_selectedIndex]->tagName();
        emit tagSelected(tagName);
    } else {
        // 如果没有选中的，但是搜索框有内容，把输入内容当成全新标签
        QString searchFilter = m_searchEdit->text().trimmed();
        if (!searchFilter.isEmpty()) {
            emit tagSelected(searchFilter);
            m_searchEdit->clear();
        }
    }
}

void TagPickerPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

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
        // 没有筛选项时按下回车，如果搜索框有文字则新建
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QString searchFilter = m_searchEdit->text().trimmed();
            if (!searchFilter.isEmpty()) {
                emit tagSelected(searchFilter);
                m_searchEdit->clear();
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
    // 处理 QLineEdit 上的按键过滤，使其支持键盘控制
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up ||
            keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right ||
            keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ||
            keyEvent->key() == Qt::Key_Escape)
        {
            // 直接由 Popover 的 keyPressEvent 统一处理
            this->keyPressEvent(keyEvent);
            return true;
        }
    }

    // 鼠标在标签项按钮上的 hover 状态同步为选中
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) {
        auto* btn = qobject_cast<TagItemButton*>(watched);
        if (btn) {
            int idx = m_visibleButtons.indexOf(btn);
            if (idx >= 0 && idx != m_selectedIndex) {
                m_selectedIndex = idx;
                updateSelectionHighlight();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ArcMeta
