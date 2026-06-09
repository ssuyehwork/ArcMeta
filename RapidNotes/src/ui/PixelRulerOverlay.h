#ifndef PIXELRULEROVERLAY_H
#define PIXELRULEROVERLAY_H

#include <QWidget>
#include <QFrame>
#include <QImage>
#include <QRect>
#include <QList>

class PixelRulerOverlay : public QWidget {
    Q_OBJECT
    
    enum Mode { Bounds, Spacing, Horizontal, Vertical };
    
    struct ScreenCapture {
        QImage image;
        QRect geometry;
        qreal dpr;
    };

public:
    explicit PixelRulerOverlay(QWidget* parent = nullptr);
    ~PixelRulerOverlay();

    bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    void initToolbar();
    void setMode(Mode m);
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // 绘制函数
    void drawCrossSpacing(QPainter& p, const QPoint& pos);
    void drawOneWaySpacing(QPainter& p, const QPoint& pos, bool hor);
    void drawLabel(QPainter& p, int x, int y, int val, bool isHor, bool isFixed = false);
    void drawBounds(QPainter& p, const QPoint& s, const QPoint& e);
    void drawInfoBox(QPainter& p, const QPoint& pos, const QString& text);
    
    // 工具函数
    int findEdge(const QImage& img, int x, int y, int dx, int dy);
    int colorDiff(const QColor& c1, const QColor& c2);
    const ScreenCapture* getCapture(const QPoint& globalPos);
    
    // 测量与保存
    QString getMeasurementText(const QPoint& pos);
    void saveMeasurement(const QString& val);

    Mode m_mode = Spacing;
    QPoint m_startPoint;
    QFrame* m_toolbar = nullptr;
    QList<ScreenCapture> m_captures;
};

#endif // PIXELRULEROVERLAY_H
