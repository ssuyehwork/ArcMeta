#ifndef FLOATINGBALL_H
#define FLOATINGBALL_H

#include <QWidget>
#include <QPoint>
#include <QPropertyAnimation>
#include <QTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "WritingAnimation.h"

class FloatingBall : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QPoint pos READ pos WRITE move)

public:
    explicit FloatingBall(QWidget* parent = nullptr);
    static QIcon generateBallIcon();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    static void renderBook(QPainter* p, const QString& skinName, float bookY);
    static void renderPen(QPainter* p, const QString& skinName, float penX, float penY, float penAngle);

private:
    void switchSkin(const QString& name);
    // drawBook 和 drawPen 已改为静态 renderBook/renderPen
    void burstParticles();
    void updatePhysics();
    void updateParticles();
    void savePosition();
    void restorePosition();

    QPoint m_pressPos;
    QPoint m_offset;
    bool m_isDragging = false;
    bool m_isHovering = false;
    bool m_isWriting = false;
    int m_writeTimer = 0;

    QTimer* m_timer;
    float m_timeStep = 0.0f;
    float m_penX = 0.0f;
    float m_penY = 0.0f;
    float m_penAngle = -45.0f;
    float m_bookY = 0.0f;

    struct Particle {
        QPointF pos;
        QPointF velocity;
        double life;
        float size;
        QColor color;
    };
    QList<Particle> m_particles;

    QString m_skinName = "mocha";

signals:
    void doubleClicked();
    void visibilityChanged(bool visible); // [NEW] 2026-04-xx 状态同步信号
    void requestMainWindow();
    void requestQuickWindow();
    void requestToolbox();
    void requestNewIdea();
};

#endif // FLOATINGBALL_H