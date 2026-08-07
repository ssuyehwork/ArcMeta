#include "TagPickerPopover.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/TagRepository.h"
#include "UiHelper.h"
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

    // 2. 文本标签（采用富文本实现 1:1 视觉对齐）
    m_textLabel = new QLabel(this);
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_textLabel, 1);

    setSelected(false);
}

void TagItemButton::setSelected(bool selected) {
    m_selected = selected;

    // 动态调整文本颜色与粗细，配合高亮变化
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
        bg = QColor("transparent");  // 默认背景透明
        border = QColor("transparent");
    }

    painter.setBrush(bg);
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(r, 4, 4); // 规范圆角：4px
}


// ============================================================================
// TagPickerPopover 实现
// ============================================================================
TagPickerPopover::TagPickerPopover(QWidget* parent)
    : QWidget(parent) // 🚨 废除 Qt::Popup，改用同窗口 Child Overlay 架构以保障 Windows IME 正常挂载与上下文畅通
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    // 物理开启输入法上下文，允许 Windows 输入法 (IME) 正常挂载
    setAttribute(Qt::WA_InputMethodEnabled, true);

    resize(320, 260); // 调整尺寸，使其在 480x360 主对话框中适配完美

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
    // 左侧：搜索图标
    m_searchEdit->addAction(UiHelper::getIcon("search", QColor("#888888"), 14), QLineEdit::LeadingPosition);
    // 右侧：网格布局与筛选按钮
    m_searchEdit->addAction(UiHelper::getIcon("layout_grid", QColor("#888888"), 14), QLineEdit::TrailingPosition);
    m_searchEdit->addAction(UiHelper::getIcon("filter_funnel_outline", QColor("#888888"), 14), QLineEdit::TrailingPosition);

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

    // 最近使用分组（QGridLayout 二列网格排布）
    m_recentGroup = new QWidget(m_scrollContainer);
    auto* recentLayout = new QVBoxLayout(m_recentGroup);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(6);

    m_recentLabel = new QLabel(m_recentGroup);
    m_recentLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    recentLayout->addWidget(m_recentLabel);

    // 1px 细分割线
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

    // 其它分组（QGridLayout 二列网格排布）
    m_globalGroup = new QWidget(m_scrollContainer);
    auto* globalLayout = new QVBoxLayout(m_globalGroup);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(6);

    m_globalLabel = new QLabel(m_globalGroup);
    m_globalLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    globalLayout->addWidget(m_globalLabel);

    // 1px 细分割线
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
    containerLayout->addWidget(m_scrollArea, 1);

    // 3. 底部提示条
    m_hintLabel = new QLabel("移动 ↑↓←→  选中 ↵  关闭 ESC", container);
    m_hintLabel->setStyleSheet("color: #666666; font-size: 11px;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(m_hintLabel);
}

TagPickerPopover::~TagPickerPopover() {
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void TagPickerPopover::showAt(const QPoint& globalPos) {
    // 每次从三源接口汇总全量去重标签集合
    m_allTags = MetadataManager::instance().getAllTags();

    // 合并分类预设标签
    auto allCats = CategoryRepo::getAll();
    for (const auto& cat : allCats) {
        for (const auto& t : cat.presetTags) {
            QString tagStr = QString::fromStdWString(t).trimmed();
            if (!tagStr.isEmpty() && !m_allTags.contains(tagStr)) {
                m_allTags[tagStr] = 0;
            }
        }
    }

    // 合并标签组标签
    auto allRepoGroups = TagRepository::getAllGroups();
    for (const auto& g : allRepoGroups) {
        for (const auto& t : g.tags) {
            QString tagStr = t.trimmed();
            if (!tagStr.isEmpty() && !m_allTags.contains(tagStr)) {
                m_allTags[tagStr] = 0;
            }
        }
    }

    // 根据引用计数频次降序排序提取前 10 个作为最近使用，其余按升序排布
    m_topTags.clear();
    QList<QPair<QString, int>> tempSorted;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        tempSorted.append({it.key(), it.value()});
    }
    // 降序排序
    std::sort(tempSorted.begin(), tempSorted.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
        return a.second > b.second;
    });

    for (int i = 0; i < qMin(10, tempSorted.size()); ++i) {
        m_topTags.append(tempSorted[i]);
    }

    m_searchEdit->clear();
    refreshList();

    // 计算父窗口中的相对位置，确保悬浮框完全容纳在父对话框内，不超出边界
    if (parentWidget()) {
        QPoint localPos = parentWidget()->mapFromGlobal(globalPos);
        int x = localPos.x();
        int y = localPos.y();

        // 保证悬浮窗不会穿透/超出父对话框
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
    raise(); // 置于最顶层 Z-order，确保遮挡下方其它控件
    m_searchEdit->setFocus();

    // 注册全局事件过滤器以实现点击外部自动关闭
    if (qApp) {
        qApp->removeEventFilter(this); // 防止重复注册
        qApp->installEventFilter(this);
    }
}

void TagPickerPopover::refreshList() {
    // 1. 清空现有的所有 button
    qDeleteAll(m_visibleButtons);
    m_visibleButtons.clear();
    m_selectedIndex = -1;

    // 清空 Grid 中的旧 items
    QLayoutItem* item;
    while ((item = m_recentGrid->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    while ((item = m_globalGrid->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QString searchFilter = m_searchEdit->text().trimmed().toLower();

    // 记录已经添加到“最近”分组的标签名，避免“其它”分组中重复展示
    QSet<QString> displayedRecentTags;

    // 2. 渲染“最近使用”分组
    int recentCount = 0;
    for (const auto& pair : m_topTags) {
        QString tagName = pair.first;
        int count = pair.second;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) {
            continue;
        }

        auto* btn = new TagItemButton(tagName, count, TagItemButton::Clock, m_recentContainer);
        m_recentGrid->addWidget(btn, recentCount / 2, recentCount % 2);
        m_visibleButtons.append(btn);
        displayedRecentTags.insert(tagName);
        recentCount++;

        // 点击事件
        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
        });

        // 鼠标 Hover 同步选中状态
        btn->installEventFilter(this);
    }

    // 3. 渲染“其它 / 全局标签库”分组
    int globalCount = 0;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        QString tagName = it.key();
        int count = it.value();

        if (displayedRecentTags.contains(tagName)) {
            continue;
        }

        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) {
            continue;
        }

        auto* btn = new TagItemButton(tagName, count, TagItemButton::CircleFilled, m_globalContainer);
        m_globalGrid->addWidget(btn, globalCount / 2, globalCount % 2);
        m_visibleButtons.append(btn);
        globalCount++;

        // 点击事件
        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            emit tagSelected(tagName);
        });

        // 鼠标 Hover 同步选中状态
        btn->installEventFilter(this);
    }

    // 4. 显示与修改标题括号及显示状态
    m_recentLabel->setText(QString("最近使用 (%1)").arg(recentCount));
    bool hasRecent = recentCount > 0;
    m_recentGroup->setVisible(hasRecent);

    m_globalLabel->setText(QString("其它 (%1)").arg(globalCount));
    bool hasGlobal = globalCount > 0;
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
    // 全局鼠标点击监听：点击 Popover 外部时自动隐藏并注销全局事件过滤器
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QPoint localPos = mapFromGlobal(globalPos);
        if (!rect().contains(localPos)) {
            hide();
            if (qApp) {
                qApp->removeEventFilter(this);
            }
            return false; // 放行点击事件，不阻塞外部操作
        }
    }

    // 处理 QLineEdit 上的按键过滤，使其支持键盘控制
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        // 核心防护：如果输入法正在弹出拼音候选框选词，100% 放行，绝不抢占 KeyPress！
        if (QGuiApplication::inputMethod()->isVisible()) {
            return false; // 放行给输入法上屏
        }

        int key = keyEvent->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up ||
            key == Qt::Key_Return || key == Qt::Key_Enter ||
            key == Qt::Key_Escape)
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
