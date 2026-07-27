#ifndef ARCMETA_FRAMELESS_WINDOW_RESIZER_H
#define ARCMETA_FRAMELESS_WINDOW_RESIZER_H

#include <QObject>
#include <QEvent>
#include <QMainWindow>
#include <QPoint>
#include <QRect>

namespace ArcMeta {

class FramelessWindowResizer : public QObject {
    Q_OBJECT

public:
    explicit FramelessWindowResizer(QMainWindow* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum ResizeDirection {
        None = 0,
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    QMainWindow* m_window;
    
    bool m_isResizing = false;
    bool m_isDragging = false;
    ResizeDirection m_resizeDir = None;
    QPoint m_resizeStartGlobal;
    QRect m_resizeStartGeometry;
    QPoint m_dragPosition;

    ResizeDirection getResizeDirection(const QPoint& pos) const;
    void updateCursorShape(ResizeDirection dir);
};

} // namespace ArcMeta

#endif // ARCMETA_FRAMELESS_WINDOW_RESIZER_H
