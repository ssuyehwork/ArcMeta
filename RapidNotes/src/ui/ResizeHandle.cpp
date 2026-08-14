#include "ResizeHandle.h"

ResizeHandle::ResizeHandle(QWidget* target, QWidget* parent) 
    : QWidget(parent), m_target(target) 
{
    setFixedSize(20, 20);
    setCursor(Qt::SizeFDiagCursor);
}

void ResizeHandle::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_startPos = event->globalPosition().toPoint();
        m_startSize = m_target->size();
        event->accept();
    }
}

void ResizeHandle::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        QPoint delta = event->globalPosition().toPoint() - m_startPos;
        int newW = qMax(m_startSize.width() + delta.x(), 600);
        int newH = qMax(m_startSize.height() + delta.y(), 400);
        m_target->resize(newW, newH);
        event->accept();
    }
}
