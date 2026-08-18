#ifndef RESIZEHANDLE_H
#define RESIZEHANDLE_H

#include <QWidget>
#include <QMouseEvent>

/**
 * @brief 隐形调整大小手柄
 */
class ResizeHandle : public QWidget {
    Q_OBJECT
public:
    explicit ResizeHandle(QWidget* target, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QWidget* m_target;
    QPoint m_startPos;
    QSize m_startSize;
};

#endif // RESIZEHANDLE_H
