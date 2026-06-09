#include "FramelessDialog.h"
#include "IconHelper.h"
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QPainter>
#include <QPen>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

#include <QMenu>
#include <QCursor>
#include <QPushButton>
#include <QLineEdit>
#include <QAbstractButton>
#include <QProgressBar>
#include <QCoreApplication>
#include "../core/ShortcutManager.h"
#include "AdvancedTagSelector.h"
#include "../core/DatabaseManager.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"

// ============================================================================

// ============================================================================
FramelessDialog::FramelessDialog(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Window)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setMouseTracking(true);

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

    setMinimumWidth(40);
    setWindowTitle(title);

    m_outerLayout = new QVBoxLayout(this);

    m_outerLayout->setContentsMargins(12, 12, 12, 12);

    m_container = new QWidget(this);
    m_container->setObjectName("DialogContainer");
    m_container->setAttribute(Qt::WA_StyledBackground);
    m_container->setStyleSheet(
        "#DialogContainer {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #333333;"
        "  border-radius: 12px;"
        "} " + StringUtils::getToolTipStyle()
    );
    m_outerLayout->addWidget(m_container);

    m_shadow = new QGraphicsDropShadowEffect(this);

    m_shadow->setBlurRadius(15);
    m_shadow->setXOffset(0);
    m_shadow->setYOffset(2);
    m_shadow->setColor(QColor(0, 0, 0, 90));
    m_container->setGraphicsEffect(m_shadow);

    m_mainLayout = new QVBoxLayout(m_container);
    m_mainLayout->setContentsMargins(0, 0, 0, 10);
    m_mainLayout->setSpacing(0);

    
    auto* titleBar = new QWidget();
    titleBar->setObjectName("TitleBar");
    titleBar->setMinimumHeight(38);
    titleBar->setStyleSheet("background-color: transparent; border-bottom: 1px solid #2D2D2D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 5, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new QLabel(title);
    m_titleLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold; border: none;");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    m_btnPin = new QPushButton();
    m_btnPin->setObjectName("btnPin");
    m_btnPin->setFixedSize(28, 28);
    m_btnPin->setIconSize(QSize(18, 18));
    m_btnPin->setAutoDefault(false);
    m_btnPin->setCheckable(true);
    m_btnPin->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa"));

    
    m_btnPin->blockSignals(true);
    m_btnPin->setChecked(m_isStayOnTop);
    if (m_isStayOnTop) {
        m_btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C"));
    }
    m_btnPin->blockSignals(false);

    m_btnPin->setStyleSheet("QPushButton { border: none; background: transparent; border-radius: 4px; } "
                          "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); } "
                          "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); } "
                          "QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");

    m_btnPin->setProperty("tooltipText", "置顶");
    m_btnPin->installEventFilter(this);
    connect(m_btnPin, &QPushButton::toggled, this, &FramelessDialog::toggleStayOnTop);
    titleLayout->addWidget(m_btnPin);

    m_minBtn = new QPushButton();
    m_minBtn->setObjectName("minBtn");
    m_minBtn->setFixedSize(28, 28);
    m_minBtn->setIconSize(QSize(18, 18));
    m_minBtn->setIcon(IconHelper::getIcon("minimize", "#888888"));
    m_minBtn->setAutoDefault(false);

    m_minBtn->setProperty("tooltipText", "最小化");
    m_minBtn->installEventFilter(this);
    m_minBtn->setCursor(Qt::PointingHandCursor);
    m_minBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_minBtn, &QPushButton::clicked, this, &QDialog::showMinimized);
    titleLayout->addWidget(m_minBtn);

    m_maxBtn = new QPushButton();
    m_maxBtn->setObjectName("maxBtn");
    m_maxBtn->setFixedSize(28, 28);
    m_maxBtn->setIconSize(QSize(16, 16));
    m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#888888"));
    m_maxBtn->setAutoDefault(false);

    m_maxBtn->setProperty("tooltipText", "最大化");
    m_maxBtn->installEventFilter(this);
    m_maxBtn->setCursor(Qt::PointingHandCursor);
    m_maxBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
    );
    connect(m_maxBtn, &QPushButton::clicked, this, &FramelessDialog::toggleMaximize);
    titleLayout->addWidget(m_maxBtn);

    m_closeBtn = new QPushButton();
    m_closeBtn->setObjectName("closeBtn");
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setIconSize(QSize(18, 18));

    m_closeBtn->setIcon(IconHelper::getIcon("close", "#FFFFFF"));
    m_closeBtn->setAutoDefault(false);

    m_closeBtn->setProperty("tooltipText", "关闭");
    m_closeBtn->installEventFilter(this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    m_closeBtn->setStyleSheet("QPushButton { background-color: #E81123; border: none; border-radius: 4px; } "
        "QPushButton:hover { background-color: #D71520; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(m_closeBtn);

    m_mainLayout->addWidget(titleBar);

    m_contentArea = new QWidget();
    m_contentArea->setObjectName("DialogContentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_contentArea->setStyleSheet("QWidget#DialogContentArea { background: transparent; border: none; }");
    m_mainLayout->addWidget(m_contentArea, 1);

    QTimer::singleShot(0, this, &FramelessDialog::updateShortcuts);
}

void FramelessDialog::setStayOnTop(bool stay) {
    if (m_btnPin) m_btnPin->setChecked(stay);
}

void FramelessDialog::updateShortcuts() {
    
    auto getScHint = [](const QString& id) -> QString {
        QKeySequence seq = ShortcutManager::instance().getShortcut(id);
        if (seq.isEmpty()) return "";
        QString keyText = seq.toString(QKeySequence::NativeText);
        keyText.replace("+", " + ");
        return QString(" （%1）").arg(keyText);
    };

    if (m_btnPin) m_btnPin->setProperty("tooltipText", "置顶" + getScHint("mw_stay_on_top"));
    if (m_closeBtn) m_closeBtn->setProperty("tooltipText", "关闭" + getScHint("qw_close"));
}

void FramelessDialog::toggleStayOnTop(bool checked) {
    m_isStayOnTop = checked;
    saveWindowSettings();

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

    if (m_btnPin) {

        m_btnPin->setIcon(IconHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", checked ? "#FF551C" : "#aaaaaa"));
    }
}

void FramelessDialog::toggleMaximize() {
    if (isMaximized()) {
        showNormal();
        m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#888888"));
        
        m_maxBtn->setProperty("tooltipText", "最大化");
    } else {
        showMaximized();

        m_maxBtn->setIcon(IconHelper::getIcon("restore_window", "#888888"));
        
        m_maxBtn->setProperty("tooltipText", "还原");
    }
}

void FramelessDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (isMaximized()) {

            m_maxBtn->setIcon(IconHelper::getIcon("restore_window", "#888888"));
            
            m_maxBtn->setProperty("tooltipText", "还原");

            m_outerLayout->setContentsMargins(0, 0, 0, 0);
            m_container->setStyleSheet(
                "#DialogContainer {"
                "  background-color: #1e1e1e;"
                "  border: none;"
                "  border-radius: 0px;"
                "} " + StringUtils::getToolTipStyle()
            );
            if (m_shadow) m_shadow->setEnabled(false);
        } else {
            m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#888888"));
            
            m_maxBtn->setProperty("tooltipText", "最大化");

            m_outerLayout->setContentsMargins(12, 12, 12, 12);
            m_container->setStyleSheet(
                "#DialogContainer {"
                "  background-color: #1e1e1e;"
                "  border: 1px solid #333333;"
                "  border-radius: 12px;"
                "} " + StringUtils::getToolTipStyle()
            );

            if (m_shadow) {
                QTimer::singleShot(50, this, [this]() {
                    if (m_shadow && !isMaximized()) {
                        m_shadow->setEnabled(true);
                        update();
                    }
                });
            }
        }

        repaint();
    }
    QDialog::changeEvent(event);
}

void FramelessDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
#ifdef Q_OS_WIN
    if (m_isStayOnTop) {
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

#ifdef Q_OS_WIN
bool FramelessDialog::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        int x = GET_X_LPARAM(msg->lParam);
        int y = GET_Y_LPARAM(msg->lParam);
        QPoint pos = mapFromGlobal(QPoint(x, y));

        
        ResizeEdge edge = getEdge(pos);
        if (edge != None) {
            switch (edge) {
                case Top:         *result = HTTOP; break;
                case Bottom:      *result = HTBOTTOM; break;
                case Left:        *result = HTLEFT; break;
                case Right:       *result = HTRIGHT; break;
                case TopLeft:     *result = HTTOPLEFT; break;
                case TopRight:    *result = HTTOPRIGHT; break;
                case BottomLeft:  *result = HTBOTTOMLEFT; break;
                case BottomRight: *result = HTBOTTOMRIGHT; break;
                default: break;
            }
            return true;
        }

        
        if (m_container) {
            QPoint containerPos = m_container->mapFrom(this, pos);
            QWidget* child = m_container->childAt(containerPos);

            if (child) {
                
                if (child->inherits("QPushButton") || child->inherits("QToolButton") ||
                    child->inherits("QLineEdit") || child->inherits("QAbstractButton")) {
                    return false;
                }

                
                QWidget* p = child;
                while (p && p != m_container) {
                    if (p->objectName() == "TitleBar") {
                        *result = HTCAPTION;
                        return true;
                    }
                    p = p->parentWidget();
                }
            }
        }
    }
    return QDialog::nativeEvent(eventType, message, result);
}
#endif

void FramelessDialog::loadWindowSettings() {
    if (objectName().isEmpty()) return;
    QSettings settings("RapidNotes", "WindowStates");
    bool stay = settings.value(objectName() + "/StayOnTop", false).toBool();
    m_isStayOnTop = stay;
    if (m_isStayOnTop) setWindowFlag(Qt::WindowStaysOnTopHint, true);

    if (m_btnPin) {
        m_btnPin->blockSignals(true);
        m_btnPin->setChecked(stay);

        m_btnPin->setIcon(IconHelper::getIcon(stay ? "pin_vertical" : "pin_tilted", stay ? "#FF551C" : "#aaaaaa"));
        m_btnPin->blockSignals(false);
    }
}

void FramelessDialog::saveWindowSettings() {
    if (objectName().isEmpty()) return;
    QSettings settings("RapidNotes", "WindowStates");
    settings.setValue(objectName() + "/StayOnTop", m_isStayOnTop);
}

void FramelessDialog::mousePressEvent(QMouseEvent* event) {
#ifndef Q_OS_WIN
    if (event->button() == Qt::LeftButton) {
        if (!isMaximized()) {
            QPoint pos = mapFromGlobal(event->globalPosition().toPoint());
            m_resizeEdge = getEdge(pos);
            if (m_resizeEdge != None) {
                m_isResizing = true;
            } else {
                m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            }
        }
        event->accept();
    } else
#endif
    if (event->button() == Qt::RightButton) {
        event->accept();
    }
}

void FramelessDialog::mouseReleaseEvent(QMouseEvent* event) {
    m_isResizing = false;
    m_resizeEdge = None;
    updateCursor(None);
    if (event->button() == Qt::RightButton) {
        event->accept();
    }
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event) {
#ifndef Q_OS_WIN
    if (isMaximized()) {
        if (cursor().shape() != Qt::ArrowCursor) setCursor(Qt::ArrowCursor);
        QDialog::mouseMoveEvent(event);
        return;
    }

    QPoint pos = mapFromGlobal(event->globalPosition().toPoint());

    if (m_isResizing) {
        QRect rect = geometry();
        QPoint globalPos = event->globalPosition().toPoint();

        int minW = minimumWidth();
        int minH = minimumHeight() > 0 ? minimumHeight() : 100;

        if (m_resizeEdge & Left) {
            int newWidth = rect.right() - globalPos.x();
            if (newWidth >= minW) rect.setLeft(globalPos.x());
        }
        if (m_resizeEdge & Right) {
            rect.setRight(globalPos.x());
        }
        if (m_resizeEdge & Top) {
            int newHeight = rect.bottom() - globalPos.y();
            if (newHeight >= minH) rect.setTop(globalPos.y());
        }
        if (m_resizeEdge & Bottom) {
            rect.setBottom(globalPos.y());
        }

        if (rect.width() >= minW && rect.height() >= minH) {
            setGeometry(rect);
        }
    } else if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPos);
    } else {
        updateCursor(getEdge(pos));
    }
#endif
    event->accept();
}

void FramelessDialog::leaveEvent(QEvent* event) {
    if (!m_isResizing) {
        updateCursor(None);
    }
    QDialog::leaveEvent(event);
}

bool FramelessDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ToolTip) {

        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 700);
        }
        return true;
    }

    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 700);
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    return QDialog::eventFilter(watched, event);
}

FramelessDialog::ResizeEdge FramelessDialog::getEdge(const QPoint& pos) {
    if (isMaximized()) return None;

    int x = pos.x();
    int y = pos.y();
    int w = width();
    int h = height();
    int edge = None;

    int margin = 12;
    int activeZone = 10; 

    if (x < 0 || x > w || y < 0 || y > h) return None;

    if (x <= activeZone) edge |= Left;
    if (x >= w - activeZone) edge |= Right;
    if (y <= activeZone) edge |= Top;
    if (y >= h - activeZone) edge |= Bottom;

    return static_cast<ResizeEdge>(edge);
}

void FramelessDialog::updateCursor(ResizeEdge edge) {
    if (isMaximized()) {
        if (cursor().shape() != Qt::ArrowCursor) setCursor(Qt::ArrowCursor);
        return;
    }

    switch (edge) {
        case Top:
        case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left:
        case Right: setCursor(Qt::SizeHorCursor); break;
        case TopLeft:
        case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight:
        case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        default:
            if (cursor().shape() != Qt::ArrowCursor) setCursor(Qt::ArrowCursor);
            break;
    }
}

void FramelessDialog::paintEvent(QPaintEvent* event) {

    
    
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QDialog::paintEvent(event);
}

void FramelessDialog::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_W) {
        reject();
    } else {
        
        
        QDialog::keyPressEvent(event);
    }
}

// ============================================================================

// ============================================================================
FramelessInputDialog::FramelessInputDialog(const QString& title, const QString& label,
                                           const QString& initial, QWidget* parent)
    : FramelessDialog(title, parent)
{
    
    resize(500, 260);
    setMinimumSize(400, 240);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);

    layout->setSpacing(7);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("color: #eee; font-size: 13px;");
    layout->addWidget(lbl);

    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    m_edit = new QLineEdit(initial);
    m_edit->installEventFilter(this);
    
    m_edit->setMinimumHeight(38);

    m_edit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D; border: 1px solid #444; border-radius: 6px;"
        "  padding: 0px 10px; color: white; selection-background-color: #4a90e2;"
        "  font-size: 14px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4a90e2; }"
    );
    layout->addWidget(m_edit);

    
    if (title.contains("标签") || label.contains("标签")) {
        m_edit->setPlaceholderText("双击调出历史标签");
        m_edit->installEventFilter(this);
    }

    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12); 
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消");
    btnCancel->setAutoDefault(false);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFixedSize(80, 32);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: transparent; color: #888; border: 1px solid #555; border-radius: 4px; } "
        "QPushButton:hover { color: #eee; border-color: #888; background-color: rgba(255, 255, 255, 0.05); }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("确定");
    btnOk->setAutoDefault(true);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setFixedSize(80, 32);
    btnOk->setStyleSheet("QPushButton { background-color: #4a90e2; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #357abd; }");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);

    m_edit->setFocus();
    m_edit->selectAll();
}

bool FramelessInputDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_edit->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_edit->setCursorPosition(m_edit->text().length());
            return true;
        }
    }
    if (watched == m_edit && event->type() == QEvent::MouseButtonDblClick) {
        auto* selector = new AdvancedTagSelector(this);

        auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
        QStringList allTags = DatabaseManager::instance().getAllTags();
        QStringList selected = m_edit->text().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
        for(QString& s : selected) s = s.trimmed();

        selector->setup(recentTags, allTags, selected);

        connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
            if (!tags.isEmpty()) {
                m_edit->setText(tags.join(", "));
                m_edit->setFocus();
            }
        });

        selector->showAtCursor();
        return true;
    }
    return FramelessDialog::eventFilter(watched, event);
}

void FramelessInputDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    QTimer::singleShot(100, m_edit, qOverload<>(&QWidget::setFocus));
}

// ============================================================================

// ============================================================================
FramelessMessageBox::FramelessMessageBox(const QString& title, const QString& text, QWidget* parent)
    : FramelessDialog(title, parent)
{

    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    resize(500, 220);
    setMinimumSize(400, 200);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(25, 20, 25, 25);
    layout->setSpacing(20);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color: #eee; font-size: 14px; line-height: 150%;");
    layout->addWidget(lbl);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消");
    btnCancel->setAutoDefault(false);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #888; border: 1px solid #555; border-radius: 4px; padding: 6px 15px; } QPushButton:hover { color: #eee; border-color: #888; }");
    connect(btnCancel, &QPushButton::clicked, this, [this](){ emit cancelled(); reject(); });
    btnLayout->addWidget(btnCancel);

    m_btnOk = new QPushButton("确定");
    m_btnOk->setAutoDefault(true); 
    m_btnOk->setCursor(Qt::PointingHandCursor);
    m_btnOk->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; border: none; border-radius: 4px; padding: 6px 20px; font-weight: bold; } QPushButton:hover { background-color: #c0392b; }");
    connect(m_btnOk, &QPushButton::clicked, this, [this](){ emit confirmed(); accept(); });
    btnLayout->addWidget(m_btnOk);

    layout->addLayout(btnLayout);
}

void FramelessMessageBox::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);

    if (m_btnOk) {
        m_btnOk->setFocus();
    }
}

// ============================================================================

// ============================================================================
FramelessProgressDialog::FramelessProgressDialog(const QString& title, const QString& label,
                                               int min, int max, QWidget* parent)
    : FramelessDialog(title, parent)
{

    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    
    resize(480, 200);
    setMinimumSize(400, 180);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(25, 20, 25, 25);
    layout->setSpacing(15);

    m_statusLabel = new QLabel(label);
    m_statusLabel->setStyleSheet("color: #eee; font-size: 13px;");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_progress = new QProgressBar();
    m_progress->setRange(min, max);
    m_progress->setValue(min);
    m_progress->setTextVisible(true);
    m_progress->setAlignment(Qt::AlignCenter);
    m_progress->setFixedHeight(24);
    m_progress->setStyleSheet(
        "QProgressBar { "
        "  background-color: #121212; "
        "  border: 1px solid #333; "
        "  border-radius: 4px; "
        "  text-align: center; "
        "  color: white; "
        "  font-weight: bold; "
        "  font-size: 11px; "
        "} "
        "QProgressBar::chunk { "
        "  background-color: #3A90FF; "
        "  border-radius: 3px; "
        "}"
    );
    layout->addWidget(m_progress);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消");
    btnCancel->setAutoDefault(false);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #888; border: 1px solid #555; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #eee; border-color: #888; }");
    connect(btnCancel, &QPushButton::clicked, this, [this](){
        m_wasCanceled = true;
        emit canceled();
        reject();
    });
    btnLayout->addWidget(btnCancel);

    layout->addLayout(btnLayout);

    
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
}

void FramelessProgressDialog::setValue(int value) {
    m_progress->setValue(value);
    
    QCoreApplication::processEvents();
}

void FramelessProgressDialog::setLabelText(const QString& text) {
    m_statusLabel->setText(text);
    QCoreApplication::processEvents();
}

void FramelessProgressDialog::setRange(int min, int max) {
    m_progress->setRange(min, max);
    QCoreApplication::processEvents();
}
