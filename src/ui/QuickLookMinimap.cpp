#include "QuickLookMinimap.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

namespace QuarkMeta {

QuickLookMinimap::QuickLookMinimap(QWidget* parent) : QWidget(parent) {
    setFixedSize(140, 90); // 极简小地图尺寸
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: transparent;");
    hide(); // 默认隐藏
}

void QuickLookMinimap::setPixmap(const QPixmap& pixmap) {
    m_pixmap = pixmap;
    if (!m_pixmap.isNull()) {
        // 保持宽高比填充在 130x80 的小地图框内
        m_scaledPixmap = m_pixmap.scaled(130, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        m_scaledPixmap = QPixmap();
    }
    update();
}

void QuickLookMinimap::clear() {
    m_pixmap = QPixmap();
    m_scaledPixmap = QPixmap();
    m_visibleRatioRect = QRectF();
    hide();
}

void QuickLookMinimap::updateViewportRect(const QRectF& visibleRect, const QRectF& totalRect) {
    if (totalRect.width() <= 0 || totalRect.height() <= 0) {
        m_visibleRatioRect = QRectF();
        return;
    }

    // 计算当前视口在大图中的归一化比例 (0.0 ~ 1.0)
    double rx = qBound(0.0, visibleRect.x() / totalRect.width(), 1.0);
    double ry = qBound(0.0, visibleRect.y() / totalRect.height(), 1.0);
    double rw = qBound(0.0, visibleRect.width() / totalRect.width(), 1.0);
    double rh = qBound(0.0, visibleRect.height() / totalRect.height(), 1.0);

    m_visibleRatioRect = QRectF(rx, ry, rw, rh);
    update();
}

void QuickLookMinimap::paintEvent(QPaintEvent*) {
    if (m_scaledPixmap.isNull()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. 绘制极暗半透明底盒 + 1px 细边框
    painter.fillRect(rect(), QColor(0, 0, 0, 180));
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

    // 2. 居中绘制大图的小缩略图
    int imgX = (width() - m_scaledPixmap.width()) / 2;
    int imgY = (height() - m_scaledPixmap.height()) / 2;
    painter.drawPixmap(imgX, imgY, m_scaledPixmap);

    // 3. 计算并绘制白色亮边视口方框 (表示当前展示的局部)
    if (!m_visibleRatioRect.isEmpty()) {
        double fx = imgX + m_visibleRatioRect.x() * m_scaledPixmap.width();
        double fy = imgY + m_visibleRatioRect.y() * m_scaledPixmap.height();
        double fw = m_visibleRatioRect.width() * m_scaledPixmap.width();
        double fh = m_visibleRatioRect.height() * m_scaledPixmap.height();

        // 确保视口框不会缩小到看不见 (最小 6x6 像素)
        fw = std::max(6.0, fw);
        fh = std::max(6.0, fh);

        QRectF frameRect(fx, fy, fw, fh);

        // 填充白色遮罩 + 白色亮边框
        painter.fillRect(frameRect, QColor(255, 255, 255, 45));
        painter.setPen(QPen(QColor("#FFFFFF"), 1.5));
        painter.drawRect(frameRect);
    }
}

void QuickLookMinimap::handleMouseInteraction(const QPoint& pos) {
    if (m_scaledPixmap.isNull()) return;

    int imgX = (width() - m_scaledPixmap.width()) / 2;
    int imgY = (height() - m_scaledPixmap.height()) / 2;

    // 计算点击位置在小缩略图上的归一化中心比例 (0.0 ~ 1.0)
    double xRatio = (pos.x() - imgX) / (double)m_scaledPixmap.width();
    double yRatio = (pos.y() - imgY) / (double)m_scaledPixmap.height();

    xRatio = qBound(0.0, xRatio, 1.0);
    yRatio = qBound(0.0, yRatio, 1.0);

    emit centerRequested(xRatio, yRatio);
}

void QuickLookMinimap::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        handleMouseInteraction(event->pos());
    }
}

void QuickLookMinimap::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        handleMouseInteraction(event->pos());
    }
}

void QuickLookMinimap::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
}

} // namespace QuarkMeta
