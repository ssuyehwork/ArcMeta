#pragma once

#include <QWidget>
#include <QPixmap>
#include <QRectF>

namespace QuarkMeta {

class QuickLookMinimap : public QWidget {
    Q_OBJECT
public:
    explicit QuickLookMinimap(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pixmap);
    void updateViewportRect(const QRectF& visibleRect, const QRectF& totalRect);
    void clear();

signals:
    // 当用户在小地图上点击/拖拽时，向主视图发送跳转比例请求 (0.0 ~ 1.0)
    void centerRequested(double xRatio, double yRatio);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void handleMouseInteraction(const QPoint& pos);

    QPixmap m_pixmap;
    QPixmap m_scaledPixmap;
    QRectF m_visibleRatioRect; // 归一化的视口矩形比例 (0.0 ~ 1.0)
    bool m_isDragging = false;
};

} // namespace QuarkMeta
