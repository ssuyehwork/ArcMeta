#include "UndoToastOverlay.h"
#include "UiHelper.h"
#include "../core/UndoManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QScreen>

namespace ArcMeta {

UndoToastOverlay* UndoToastOverlay::instance() {
    static UndoToastOverlay* inst = new UndoToastOverlay(nullptr);
    return inst;
}

UndoToastOverlay::UndoToastOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);

    // 1. 成功绿勾图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(18, 18);
    m_iconLabel->setPixmap(UiHelper::getIcon("check_circle_filled", QColor("#2ECC71")).pixmap(18, 18));
    layout->addWidget(m_iconLabel);

    // 2. 消息文案
    m_msgLabel = new QLabel(this);
    m_msgLabel->setStyleSheet("color: #EEEEEE; font-size: 12px; font-family: 'Segoe UI', 'Microsoft YaHei';");
    layout->addWidget(m_msgLabel);

    // 3. 撤销按钮
    m_btnUndo = new QPushButton("撤销", this);
    m_btnUndo->setCursor(Qt::PointingHandCursor);
    m_btnUndo->setStyleSheet(
        "QPushButton { color: #FFFFFF; font-weight: bold; font-size: 12px; border: none; background: transparent; text-decoration: underline; }"
        "QPushButton:hover { color: #FF551C; }"
    );
    layout->addWidget(m_btnUndo);

    // 4. 垂直分割线
    m_separator = new QWidget(this);
    m_separator->setFixedSize(1, 14);
    m_separator->setStyleSheet("background-color: #4E4E52;");
    layout->addWidget(m_separator);

    // 5. 关闭按钮
    m_btnClose = new QPushButton("×", this);
    m_btnClose->setFixedSize(16, 16);
    m_btnClose->setCursor(Qt::PointingHandCursor);
    m_btnClose->setStyleSheet(
        "QPushButton { color: #888888; font-size: 14px; font-weight: bold; border: none; background: transparent; }"
        "QPushButton:hover { color: #FFFFFF; }"
    );
    layout->addWidget(m_btnClose);

    // 定时器与动画
    m_autoHideTimer.setSingleShot(true);
    connect(&m_autoHideTimer, &QTimer::timeout, this, &UndoToastOverlay::hideToast);

    m_fadeAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnim->setDuration(200);

    // 按钮事件绑定
    connect(m_btnUndo, &QPushButton::clicked, this, [this]() {
        // 点击气泡“撤销”时，物理对位统一并轨并分发至全局 UndoManager
        UndoManager::instance().undo();
        hideToast();
    });

    connect(m_btnClose, &QPushButton::clicked, this, &UndoToastOverlay::hideToast);

    hide();
}

void UndoToastOverlay::showToast(QWidget* parent, const QString& message, std::function<void()> undoCallback, int durationMs) {
    // 🚨 核心修复：淡入前强行断开之前遗留的一切 finished 信号绑定，防止淡入动画结束后误触发上一轮的隐藏闭包！
    m_fadeAnim->stop();
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);

    // 依然接收 undoCallback 用于判断是否可见“撤销”按钮（若为 nullptr 代表该操作物理不可逆），但实际执行已物理并轨
    m_undoCallback = undoCallback;
    m_msgLabel->setText(message);
    m_btnUndo->setVisible(m_undoCallback != nullptr);
    m_separator->setVisible(m_undoCallback != nullptr);

    adjustSize();

    // 计算定位：位于 Screen/Parent 底部居中（距离底边 40px）
    QPoint targetPos;
    if (parent) {
        QRect parentGeom = parent->geometry();
        QPoint parentGlobal = parent->mapToGlobal(QPoint(0, 0));
        int x = parentGlobal.x() + (parentGeom.width() - width()) / 2;
        int y = parentGlobal.y() + parentGeom.height() - height() - 40;
        targetPos = QPoint(x, y);
    } else {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen) {
            screen = QApplication::primaryScreen();
        }
        if (screen) {
            QRect screenGeom = screen->geometry();
            int x = screenGeom.x() + (screenGeom.width() - width()) / 2;
            int y = screenGeom.y() + screenGeom.height() - height() - 60;
            targetPos = QPoint(x, y);
        } else {
            targetPos = QPoint(100, 100);
        }
    }

    move(targetPos);

    // 淡入显示
    setWindowOpacity(0.0);
    show();
    raise();

    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();

    m_autoHideTimer.start(durationMs);
}

void UndoToastOverlay::hideToast() {
    m_autoHideTimer.stop();
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(windowOpacity());
    m_fadeAnim->setEndValue(0.0);
    
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        hide();
        setWindowOpacity(1.0);
    });
    m_fadeAnim->start();
}

void UndoToastOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF rect(0.5, 0.5, width() - 1, height() - 1);
    p.setPen(QPen(QColor("#3E3E42"), 1));
    p.setBrush(QColor("#252526"));
    p.drawRoundedRect(rect, 6, 6);
}

} // namespace ArcMeta
