#include "DriveButton.h"
#include "SvgIcons.h"
#include "StyleLibrary.h"
#include <QPainter>
#include <QDebug>

namespace ArcMeta {

DriveButton::DriveButton(const QString& letter, QWidget* parent)
    : QPushButton(letter, parent), m_letter(letter) {
    setCheckable(true);
    setFixedSize(60, 26); // 稍微加宽以容纳图标
    setCursor(Qt::PointingHandCursor);

    // 初始化 SVG 渲染器
    QString refreshSvg = SvgIcons::icons.value("refresh");
    if (!refreshSvg.isEmpty()) {
        m_refreshRenderer = std::make_unique<QSvgRenderer>();
        m_refreshRenderer->load(refreshSvg.toLatin1());
    }
    
    QString pauseSvg = SvgIcons::icons.value("pause");
    if (!pauseSvg.isEmpty()) {
        m_pauseRenderer = std::make_unique<QSvgRenderer>();
        m_pauseRenderer->load(pauseSvg.toLatin1());
    }

    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &DriveButton::onAnimationTimeout);

    updateStyle();
}

void DriveButton::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    
    // 内部同步 Loading 状态以兼容 Plan-97 绘制逻辑
    setLoading(m_state == Running);
    
    updateStyle();
    update();
}

void DriveButton::setLoading(bool loading) {
    if (m_isLoading == loading) return;
    m_isLoading = loading;
    if (m_isLoading) {
        m_animationTimer->start(30);
    } else {
        m_animationTimer->stop();
        m_rotationAngle = 0;
    }
    update();
}

void DriveButton::updateStyle() {
    // 强制设置 checked 状态以匹配 QSS
    blockSignals(true);
    setChecked(m_state != Inactive);
    blockSignals(false);

    QString color = (m_state == Inactive) ? "#333333" : Style::qssColor(Style::PrimaryBlue);
    
    setStyleSheet(QString(
        "QPushButton { background-color: %1; border: 1px solid #444; border-radius: 4px; color: #CCC; font-size: 11px; font-weight: bold; padding-left: 5px; }"
        "QPushButton:hover { background-color: #3E3E42; border-color: %2; }"
        "QPushButton:checked { background-color: %3; color: #FFF; border-color: %3; }"
    ).arg(color).arg(Style::qssColor(Style::PrimaryBlue)).arg(Style::qssColor(Style::PrimaryBlue)));
}

void DriveButton::onAnimationTimeout() {
    m_rotationAngle = (m_rotationAngle + 10) % 360;
    update();
}

void DriveButton::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    int iconSize = 14;
    int x = width() - iconSize - 6;
    int y = (height() - iconSize) / 2;

    if (m_state == Running && m_refreshRenderer && m_refreshRenderer->isValid()) {
        painter.save();
        painter.translate(x + iconSize / 2, y + iconSize / 2);
        painter.rotate(m_rotationAngle);
        painter.translate(-(x + iconSize / 2), -(y + iconSize / 2));
        m_refreshRenderer->render(&painter, QRect(x, y, iconSize, iconSize));
        painter.restore();
    } 
    else if (m_state == Paused && m_pauseRenderer && m_pauseRenderer->isValid()) {
        m_pauseRenderer->render(&painter, QRect(x, y, iconSize, iconSize));
    }
}

} // namespace ArcMeta
