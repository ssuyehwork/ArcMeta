#include "TagSelectorOverlay.h"
#include "UiHelper.h"
#include "../meta/MetadataManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>

namespace ArcMeta {

TagSelectorOverlay::TagSelectorOverlay(const QStringList& initialSelected, QWidget* parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), 
      m_selectedTags(initialSelected) 
{
    setObjectName("TagSelectorOverlay");
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(
        "QFrame#TagSelectorOverlay {"
        "  background-color: #1E1E1E;"
        "  border: 1px solid #333333;"
        "  border-radius: 6px;"
        "}"
    );
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_DeleteOnClose, false); // 让调用者决定生命周期或通过失焦安全销毁

    initUi();
    loadTagsAndGroups();
    
    // 安装事件过滤器
    m_searchEdit->installEventFilter(this);
    m_tagGridWidget->installEventFilter(this);
}

TagSelectorOverlay::~TagSelectorOverlay() {
}

void TagSelectorOverlay::initUi() {
    QVBoxLayout* mainL = new QVBoxLayout(this);
    mainL->setContentsMargins(10, 10, 10, 10);
    mainL->setSpacing(8);

    // 顶部拖拽手柄栏
    QWidget* topHandle = new QWidget(this);
    topHandle->setFixedHeight(8);
    topHandle->setStyleSheet("background: transparent;");
    mainL->addWidget(topHandle);

    // 搜索与创建栏
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索或新建标签...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(26);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: #151515; border: 1px solid #333; border-radius: 4px; padding: 0 8px; color: #EEE; font-size: 11px; }"
        "QLineEdit:focus { border-color: #1C97EA; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        filterTags();
    });
    mainL->addWidget(m_searchEdit);

    // 中部双视口（左侧群组、右侧标签）
    QHBoxLayout* bodyL = new QHBoxLayout();
    bodyL->setSpacing(8);

    // 左侧群组列表
    m_groupList = new QListWidget(this);
    m_groupList->setFixedWidth(110);
    m_groupList->setFocusPolicy(Qt::StrongFocus);
    m_groupList->setStyleSheet(
        "QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 4px; outline: none; padding: 2px; }"
        "QListWidget::item { height: 26px; color: #BBB; border-radius: 3px; padding-left: 6px; font-size: 11px; }"
        "QListWidget::item:hover { background-color: #2D2D30; color: #FFF; }"
        "QListWidget::item:selected { background-color: #3E3E42; color: #1C97EA; font-weight: bold; }"
    );
    connect(m_groupList, &QListWidget::currentRowChanged, this, [this]() {
        filterTags();
    });
    bodyL->addWidget(m_groupList);

    // 右侧标签面板 (支持纯键盘操作网格)
    m_tagGridWidget = new QWidget(this);
    m_tagGridWidget->setFocusPolicy(Qt::StrongFocus);
    m_gridFlowLayout = new FlowLayout(m_tagGridWidget, 0, 4, 4);
    m_tagGridWidget->setLayout(m_gridFlowLayout);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: 1px solid #333; background: transparent; border-radius: 4px; }");
    m_scrollArea->setWidget(m_tagGridWidget);
    bodyL->addWidget(m_scrollArea, 1);

    mainL->addLayout(bodyL, 1);

    // 底部快捷键提示栏
    QWidget* bottomBar = new QWidget(this);
    bottomBar->setFixedHeight(22);
    bottomBar->setStyleSheet("background-color: #151515; border-radius: 3px;");
    QHBoxLayout* bottomL = new QHBoxLayout(bottomBar);
    bottomL->setContentsMargins(8, 0, 8, 0);

    QLabel* helpTips = new QLabel(bottomBar);
    helpTips->setText("切换 <font color='#1C97EA'><b>Tab</b></font>    移动 <font color='#1C97EA'><b>↑↓←→</b></font>    选中/新建 <font color='#1C97EA'><b>⏎</b></font>");
    helpTips->setStyleSheet("color: #888; font-size: 10px;");
    bottomL->addWidget(helpTips);

    bottomL->addStretch();

    QLabel* closeTips = new QLabel("关闭 ESC", bottomBar);
    closeTips->setStyleSheet("color: #888; font-size: 10px;");
    bottomL->addWidget(closeTips);

    mainL->addWidget(bottomBar);

    resize(400, 240); // 初始大小
}

void TagSelectorOverlay::loadTagsAndGroups() {
    m_allTagCounts = MetadataManager::instance().getAllTags();
    
    m_groupList->clear();
    m_groupList->addItem("全部");
    m_groupList->addItem("未分类");
    m_groupList->addItem("最近使用");

    m_groupList->setCurrentRow(0);
}

void TagSelectorOverlay::filterTags() {
    QString kw = m_searchEdit->text().trimmed();
    QString currentGrp = m_groupList->currentItem() ? m_groupList->currentItem()->text() : "全部";

    m_displayedTags.clear();
    for (auto it = m_allTagCounts.begin(); it != m_allTagCounts.end(); ++it) {
        QString tag = it.key();
        int count = it.value();

        if (!kw.isEmpty() && !tag.toLower().contains(kw.toLower())) continue;

        if (currentGrp == "未分类" && count > 2) continue;
        if (currentGrp == "最近使用" && count < 3) continue;

        m_displayedTags.append(tag);
    }

    // 如果搜索框不为空且当前不存在完全一样的标签，则支持回车新建
    if (!kw.isEmpty() && !m_displayedTags.contains(kw, Qt::CaseInsensitive)) {
        m_displayedTags.prepend(kw); // 放在第一位作为待建/候选
    }

    populateGrid();
}

void TagSelectorOverlay::populateGrid() {
    // 清理旧网格
    QLayoutItem* item;
    while ((item = m_gridFlowLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    m_tagButtons.clear();
    for (int i = 0; i < m_displayedTags.size(); ++i) {
        QString tag = m_displayedTags[i];

        QPushButton* btn = new QPushButton(m_tagGridWidget);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);

        m_tagButtons.append(btn);
        m_gridFlowLayout->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, tag]() {
            toggleTagSelection(tag);
        });
    }

    m_currentTagIndex = m_displayedTags.isEmpty() ? -1 : 0;
    updateSelectionHighlight();
}

void TagSelectorOverlay::toggleTagSelection(const QString& tag) {
    QString cleanTag = tag.trimmed();
    if (cleanTag.isEmpty()) return;

    if (m_selectedTags.contains(cleanTag)) {
        m_selectedTags.removeAll(cleanTag);
    } else {
        m_selectedTags.append(cleanTag);
        // 如果是新建的标签，顺便加入内存，使其能出现在“最近使用”或“全部”中
        if (!m_allTagCounts.contains(cleanTag)) {
            m_allTagCounts[cleanTag] = 1;
        }
    }
    updateSelectionHighlight();
    emit selectionChanged(m_selectedTags);
}

void TagSelectorOverlay::updateSelectionHighlight() {
    for (int i = 0; i < m_displayedTags.size(); ++i) {
        QString tag = m_displayedTags[i];
        int count = m_allTagCounts.value(tag, 0);
        QPushButton* btn = m_tagButtons[i];

        bool isSelected = m_selectedTags.contains(tag);
        bool isFocused = (i == m_currentTagIndex);

        QString prefix = isSelected ? "✓ " : "• ";
        if (count == 0) {
            btn->setText(QString("+ 新建 \"%1\"").arg(tag));
        } else {
            btn->setText(QString("%1%2 (%3)").arg(prefix).arg(tag).arg(count));
        }

        QString style;
        if (isSelected) {
            style = "QPushButton { background-color: #1C97EA; color: #FFF; border: 1px solid #1C97EA; border-radius: 11px; padding: 0 10px; font-size: 11px; }";
        } else {
            style = "QPushButton { background-color: transparent; color: #BBB; border: 1px solid #333; border-radius: 11px; padding: 0 10px; font-size: 11px; }";
        }

        if (isFocused) {
            style += " QPushButton { border: 1px solid #1C97EA; color: #FFF; background-color: #2D2D30; }";
        } else {
            style += " QPushButton:hover { border-color: #1ABC9C; color: #FFF; }";
        }
        btn->setStyleSheet(style);
    }
}

void TagSelectorOverlay::handleGridNavigation(int key) {
    if (m_displayedTags.isEmpty()) return;
    int rowCount = m_displayedTags.size();
    if (key == Qt::Key_Left) {
        m_currentTagIndex = (m_currentTagIndex - 1 + rowCount) % rowCount;
    } else if (key == Qt::Key_Right) {
        m_currentTagIndex = (m_currentTagIndex + 1) % rowCount;
    } else if (key == Qt::Key_Up) {
        m_currentTagIndex = qMax(0, m_currentTagIndex - 4);
    } else if (key == Qt::Key_Down) {
        m_currentTagIndex = qMin(rowCount - 1, m_currentTagIndex + 4);
    }
    updateSelectionHighlight();
}

void TagSelectorOverlay::handleGroupNavigation(int key) {
    int row = m_groupList->currentRow();
    int count = m_groupList->count();
    if (key == Qt::Key_Up) {
        m_groupList->setCurrentRow((row - 1 + count) % count);
    } else if (key == Qt::Key_Down) {
        m_groupList->setCurrentRow((row + 1) % count);
    }
}

// -------------------------------------------------------------------------
// 8方向拉伸大小和拖拽移动实现
// -------------------------------------------------------------------------
int TagSelectorOverlay::getResizeDirection(const QPoint& pos) {
    int dir = 0;
    if (pos.x() < m_margin) dir |= 1; // Left
    else if (pos.x() > width() - m_margin) dir |= 2; // Right
    
    if (pos.y() < m_margin) dir |= 4; // Top
    else if (pos.y() > height() - m_margin) dir |= 8; // Bottom
    return dir;
}

void TagSelectorOverlay::updateCursorShape(const QPoint& pos) {
    int dir = getResizeDirection(pos);
    if (dir == 0) {
        if (pos.y() < 25) { // 顶部手柄区
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    } else {
        if (dir == (1 | 4) || dir == (2 | 8)) setCursor(Qt::SizeFDiagCursor); // Top-Left or Bottom-Right
        else if (dir == (2 | 4) || dir == (1 | 8)) setCursor(Qt::SizeBDiagCursor); // Top-Right or Bottom-Left
        else if (dir == 1 || dir == 2) setCursor(Qt::SizeHorCursor); // Left or Right
        else if (dir == 4 || dir == 8) setCursor(Qt::SizeVerCursor); // Top or Bottom
    }
}

void TagSelectorOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_resizeDir = getResizeDirection(event->pos());
        m_isDragging = (m_resizeDir == 0 && event->pos().y() < 25);
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartGeometry = geometry();
        event->accept();
    } else {
        QFrame::mousePressEvent(event);
    }
}

void TagSelectorOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        if (m_isDragging) {
            move(m_dragStartGeometry.topLeft() + delta);
        } else if (m_resizeDir != 0) {
            QRect newGeom = m_dragStartGeometry;
            if (m_resizeDir & 1) { // Left
                newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
            } else if (m_resizeDir & 2) { // Right
                newGeom.setRight(m_dragStartGeometry.right() + delta.x());
            }

            if (m_resizeDir & 4) { // Top
                newGeom.setTop(m_dragStartGeometry.top() + delta.y());
            } else if (m_resizeDir & 8) { // Bottom
                newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
            }

            // 限制最小尺寸
            if (newGeom.width() >= 250 && newGeom.height() >= 150) {
                setGeometry(newGeom);
            }
        }
        event->accept();
    } else {
        updateCursorShape(event->pos());
        QFrame::mouseMoveEvent(event);
    }
}

void TagSelectorOverlay::mouseReleaseEvent(QMouseEvent* event) {
    m_isDragging = false;
    m_resizeDir = 0;
    setCursor(Qt::ArrowCursor);
    QFrame::mouseReleaseEvent(event);
}

// -------------------------------------------------------------------------
// 核心交互：失焦关闭与回车防击穿
// -------------------------------------------------------------------------
void TagSelectorOverlay::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow()) {
            emit overlayClosed();
            close();
            deleteLater();
        }
    }
    QFrame::changeEvent(event);
}

bool TagSelectorOverlay::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        
        // 1. ESC 关闭
        if (ke->key() == Qt::Key_Escape) {
            emit overlayClosed();
            close();
            deleteLater();
            ke->accept();
            return true;
        }

        // 2. Tab 切换焦点
        if (ke->key() == Qt::Key_Tab) {
            if (m_searchEdit->hasFocus()) {
                m_groupList->setFocus();
            } else if (m_groupList->hasFocus()) {
                m_tagGridWidget->setFocus();
            } else {
                m_searchEdit->setFocus();
            }
            updateSelectionHighlight();
            ke->accept();
            return true;
        }

        // 3. 方向键导航
        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) {
            if (m_tagGridWidget->hasFocus() || obj == m_tagGridWidget) {
                handleGridNavigation(ke->key());
                ke->accept();
                return true;
            } else if (m_groupList->hasFocus()) {
                handleGroupNavigation(ke->key());
                ke->accept();
                return true;
            }
        }

        // 4. 回车事件严格物理吞噬（防双闪退/击穿连选）
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (m_searchEdit->hasFocus()) {
                QString kw = m_searchEdit->text().trimmed();
                if (!kw.isEmpty()) {
                    toggleTagSelection(kw);
                    m_searchEdit->clear();
                }
            } else if (m_tagGridWidget->hasFocus() || obj == m_tagGridWidget) {
                if (m_currentTagIndex >= 0 && m_currentTagIndex < m_displayedTags.size()) {
                    toggleTagSelection(m_displayedTags[m_currentTagIndex]);
                }
            }
            ke->accept();
            return true; // 彻底阻断任何键盘事件向上冒泡到父窗口
        }
    }
    return QFrame::eventFilter(obj, event);
}

} // namespace ArcMeta
