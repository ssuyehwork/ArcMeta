#include "ColorPill.h"
#include "../ToolTipOverlay.h"
#include "../UiHelper.h"
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QClipboard>
#include <QApplication>

namespace QuarkMeta {

ColorPill::ColorPill(const QColor& color, float ratio, QWidget* parent) : QWidget(parent) {
    setFixedSize(16, 16);
    setCursor(Qt::PointingHandCursor);
    setData(color, ratio);
}

void ColorPill::setData(const QColor& color, float ratio) {
    m_color = color;
    m_ratio = ratio;
    update();
}

void ColorPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawRoundedRect(rect(), 4, 4);
    if (m_hovered) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    }
}

void ColorPill::enterEvent(QEnterEvent*) {
    m_hovered = true;
    QString hex = m_color.name().toUpper();
    int ratio = qRound(m_ratio * 100);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("%1 (%2%)").arg(hex).arg(ratio), 0);
    update();
}

void ColorPill::leaveEvent(QEvent*) {
    m_hovered = false;
    ToolTipOverlay::hideTip();
    update();
}

void ColorPill::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        emit colorSelected(m_color);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);
        QColor color = m_color;
        menu.addAction("搜索相似颜色的项目", [this, color]() { emit colorSelected(color); });
        menu.addSeparator();
        QString hex = color.name().toUpper();
        menu.addAction(QString("复制 %1").arg(hex), [hex]() { QApplication::clipboard()->setText(hex); });
        menu.addSeparator();
        menu.addAction("设置为自定义主色", [this, color]() { emit requestSetAsPrimary(color); });
        menu.exec(event->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(event);
}

} // namespace QuarkMeta
