#include "FramelessDialog.h"
#include "UiHelper.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QApplication>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace ArcMeta {

// ============================================================================
// FramelessDialog 基类实现
// ============================================================================
FramelessDialog::FramelessDialog(const QString& title, QWidget* parent) 
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Window) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    setWindowTitle(title);

    // 2026-05-10 对标规范：通过 DwmSetWindowAttribute 开启 Windows 11 原生圆角
    DWORD attribute = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 33, &attribute, sizeof(attribute));

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);

    m_container = new QWidget(this);
    m_container->setObjectName("DialogContainer");
    m_container->setAttribute(Qt::WA_StyledBackground);
    m_container->setStyleSheet(
        "#DialogContainer {"
        "  background-color: #1E1E1E;"
        "  border: 1px solid #333333;"
        "  border-radius: 12px;"
        "}"
    );
    m_outerLayout->addWidget(m_container);

    m_mainLayout = new QVBoxLayout(m_container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // --- 标题栏 ---
    auto* titleBar = new QWidget();
    titleBar->setObjectName("TitleBar");
    titleBar->setFixedHeight(38);
    // 标题栏背景保持与主界面一致的深色，作为整体容器的一部分
    titleBar->setStyleSheet("background-color: transparent; border-bottom: 1px solid #2D2D2D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 5, 0);
    titleLayout->setSpacing(4);

    m_titleLabel = new QLabel(title);
    m_titleLabel->setStyleSheet("color: #AAAAAA; font-size: 12px; font-weight: bold; border: none;");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    auto createTitleBtn = [this](const QString& iconName, const QString& tooltip, const QString& hoverColor) {
        QPushButton* btn = new QPushButton();
        // 2026-05-10 对标规范：尺寸固定为 32x32，margin 设为 2px，border-radius: 4px
        btn->setFixedSize(32, 32);
        btn->setIcon(UiHelper::getIcon(iconName, QColor("#CCCCCC"), 14));
        btn->setIconSize(QSize(14, 14));
        btn->setAutoDefault(false);
        btn->setProperty("tooltipText", tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(
            "QPushButton { background-color: transparent; border: none; border-radius: 4px; margin: 2px; } "
            "QPushButton:hover { background-color: %1; } "
            "QPushButton:pressed { background-color: #555555; }"
        ).arg(hoverColor));
        btn->installEventFilter(this);
        return btn;
    };

    m_pinBtn = createTitleBtn("pin_tilted", "置顶", "#333333");
    // 2026-05-10 置顶按钮逻辑规范
    m_pinBtn->setCheckable(true);
    m_pinBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; border-radius: 4px; margin: 2px; } "
        "QPushButton:hover { background-color: #333333; } "
        "QPushButton:checked { background-color: #4A90E2; }" // 高亮反馈
    );
    connect(m_pinBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_pinBtn->setIcon(UiHelper::getIcon(checked ? "pin" : "pin_tilted", QColor("#CCCCCC"), 14));
        // 设置/取消置顶
        setWindowFlag(Qt::WindowStaysOnTopHint, checked);
        show();
    });

    m_minBtn = createTitleBtn("minimize", "最小化", "#333333");
    connect(m_minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

    m_maxBtn = createTitleBtn("maximize", "最大化", "#333333");
    connect(m_maxBtn, &QPushButton::clicked, this, [this]() {
        if (isMaximized()) {
            showNormal();
            m_maxBtn->setIcon(UiHelper::getIcon("maximize", QColor("#CCCCCC"), 14));
        } else {
            showMaximized();
            m_maxBtn->setIcon(UiHelper::getIcon("restore", QColor("#CCCCCC"), 14));
        }
    });

    m_closeBtn = new QPushButton();
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setIcon(UiHelper::getIcon("close", QColor("#FFFFFF"), 14));
    m_closeBtn->setIconSize(QSize(14, 14));
    m_closeBtn->setAutoDefault(false);
    m_closeBtn->setProperty("tooltipText", "关闭");
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        "QPushButton { background-color: #E81123; border: none; border-radius: 4px; margin: 2px; } "
        "QPushButton:hover { background-color: #F1707A; } "
        "QPushButton:pressed { background-color: #A50000; }"
    );
    m_closeBtn->installEventFilter(this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 2026-05-10 对标规范：从右到左排列 (布局顺序: pin -> min -> max -> close)
    titleLayout->addWidget(m_pinBtn);
    titleLayout->addWidget(m_minBtn);
    titleLayout->addWidget(m_maxBtn);
    titleLayout->addWidget(m_closeBtn);

    m_mainLayout->addWidget(titleBar);

    m_contentArea = new QWidget();
    m_contentArea->setObjectName("DialogContentArea");
    m_contentArea->setStyleSheet("QWidget#DialogContentArea { background: transparent; border: none; }");
    m_mainLayout->addWidget(m_contentArea, 1);
}

void FramelessDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
}

void FramelessDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 判定是否在标题栏区域拖拽
        QWidget* child = childAt(event->pos());
        if (child) {
            bool inTitleBar = false;
            QWidget* p = child;
            while (p && p != m_container) {
                if (p->objectName() == "TitleBar") {
                    inTitleBar = true;
                    break;
                }
                p = p->parentWidget();
            }
            
            // 排除掉标题栏上的按钮
            if (inTitleBar && !qobject_cast<QPushButton*>(child)) {
                m_isDragging = true;
                m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
                event->accept();
                return;
            }
        }
    }
    QDialog::mousePressEvent(event);
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void FramelessDialog::mouseReleaseEvent(QMouseEvent* event) {
    m_isDragging = false;
    QDialog::mouseReleaseEvent(event);
}


void FramelessDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // 物理还原两段式 UX：若有非空输入框则先清空，否则关闭
        QLineEdit* edit = findChild<QLineEdit*>();
        if (edit && edit->isVisible() && !edit->text().isEmpty()) {
            edit->clear();
            event->accept();
            return;
        }
        reject();
    } else {
        QDialog::keyPressEvent(event);
    }
}

bool FramelessDialog::eventFilter(QObject* watched, QEvent* event) {
    return QDialog::eventFilter(watched, event);
}

// ============================================================================
// FramelessInputDialog 实现
// ============================================================================
FramelessInputDialog::FramelessInputDialog(const QString& title, const QString& label, 
                                           const QString& initial, QWidget* parent)
    : FramelessDialog(title, parent) 
{
    // 按照用户最新要求：高度减去 50 像素 (260 -> 210)
    resize(500, 210);
    setMinimumSize(400, 190);
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(7);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet("color: #EEEEEE; font-size: 13px;");
    layout->addWidget(lbl);

    m_edit = new QLineEdit(initial);
    m_edit->setMinimumHeight(38);
    // 严格对齐 RapidNotes 输入框风格：深色背景 + 蓝色聚焦边框，应用 6px 圆角
    m_edit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D; border: 1px solid #444; border-radius: 6px;"
        "  padding: 0px 10px; color: white; selection-background-color: #4A90E2;"
        "  font-size: 14px;"
        "}"
        "QLineEdit:focus { border: 1px solid #4A90E2; }"
    );
    layout->addWidget(m_edit);

    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    auto* btnCancel = new QPushButton("取消");
    btnCancel->setFixedSize(80, 32);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: transparent; color: #888; border: 1px solid #444; border-radius: 4px; } "
        "QPushButton:hover { color: #EEE; background-color: #333; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("确定");
    btnOk->setFixedSize(80, 32);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet(
        "QPushButton { background-color: #4A90E2; color: white; border: none; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #3E3E42; }" // 统一悬停色
    );
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);

    m_edit->setFocus();
    m_edit->selectAll();
}

void FramelessInputDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    QTimer::singleShot(50, m_edit, qOverload<>(&QWidget::setFocus));
}

} // namespace ArcMeta
