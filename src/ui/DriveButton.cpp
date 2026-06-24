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

    // 初始化 SVG 渲染器 (复用 LoadingWindow 逻辑)
    QString refreshSvg = SvgIcons::icons.value("refresh");
    if (!refreshSvg.isEmpty()) {
        m_svgRenderer = std::make_unique<QSvgRenderer>();
        m_svgRenderer->load(refreshSvg.toLatin1());
    }

    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &DriveButton::onAnimationTimeout);

    // 基础样式
    setStyleSheet(QString(
        "QPushButton { background-color: #333333; border: 1px solid #444; border-radius: 4px; color: #CCC; font-size: 11px; font-weight: bold; padding-left: 5px; }"
        "QPushButton:hover { background-color: #3E3E42; border-color: %1; }"
        "QPushButton:checked { background-color: %1; color: #FFF; border-color: %1; }"
    ).arg(Style::qssColor(Style::PrimaryBlue)));
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

void DriveButton::onAnimationTimeout() {
    m_rotationAngle = (m_rotationAngle + 10) % 360;
    update();
}

void DriveButton::paintEvent(QPaintEvent* event) {
    QPushButton::paintEvent(event);

    if (m_isLoading && m_svgRenderer && m_svgRenderer->isValid()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        int iconSize = 14;
        // 绘制在右侧
        int x = width() - iconSize - 6;
        int y = (height() - iconSize) / 2;

        painter.save();
        painter.translate(x + iconSize / 2, y + iconSize / 2);
        painter.rotate(m_rotationAngle);
        painter.translate(-(x + iconSize / 2), -(y + iconSize / 2));
        m_svgRenderer->render(&painter, QRect(x, y, iconSize, iconSize));
        painter.restore();
    }
}

} // namespace ArcMeta
