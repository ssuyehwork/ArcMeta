#include "TagPill.h"
#include "../UiHelper.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QFontMetrics>

namespace ArcMeta {

TagPill::TagPill(const QString& text, QWidget* parent) : QWidget(parent), m_text(text) {
    setFixedHeight(22);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(4);
    m_label = new QLabel(text, this);
    m_label->setStyleSheet("color: #EEEEEE; font-size: 12px; border: none; background: transparent;");
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFixedSize(14, 14);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setIcon(UiHelper::getIcon("close", QColor("#B0B0B0"), 12));
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: #3E3E42; border-radius: 2px; }");
    layout->addWidget(m_label);
    layout->addWidget(m_closeBtn);
    connect(m_closeBtn, &QPushButton::clicked, [this]() { emit deleteRequested(m_text); });
    setData(text);
}

void TagPill::setData(const QString& text) {
    m_text = text;
    setProperty("tagText", text);
    m_label->setText(text);
    QFontMetrics fm(m_label->font());
    setFixedWidth(fm.horizontalAdvance(text) + 30);
}

void TagPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#2D2D30"));
    painter.setPen(QPen(QColor("#3E3E42"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 2, 2);
}

} // namespace ArcMeta
