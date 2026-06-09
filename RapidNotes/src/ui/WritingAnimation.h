#ifndef WRITINGANIMATION_H
#define WRITINGANIMATION_H

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QtMath>

class WritingAnimation : public QWidget {
    Q_OBJECT
public:
    explicit WritingAnimation(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(40, 40);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &WritingAnimation::updatePhysics);
    }

    void start() {
        m_time = 0;
        m_timer->start(20);
        show();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        float cx = width() / 2.0f;
        float cy = height() / 2.0f;

        // 1. 绘制书本
        p.save();
        p.translate(cx, cy + m_bookY);
        p.scale(0.4, 0.4);
        drawBook(&p);
        p.restore();

        // 2. 绘制钢笔
        p.save();
        p.translate(cx + m_penX, cy + m_penY - 5);
        p.scale(0.4, 0.4);
        p.rotate(m_angle);
        drawPen(&p);
        p.restore();
    }

private:
    void drawBook(QPainter* p) {
        // 模仿 Python 版的 Mocha 书本
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#f5f0e1")); // 纸张
        p->drawRoundedRect(QRectF(-22, -32, 56, 76), 3, 3);
        
        QLinearGradient grad(-28, -38, 28, 38);
        grad.setColorAt(0, QColor("#5a3c32"));
        grad.setColorAt(1, QColor("#321e19"));
        p->setBrush(grad); // 封面
        p->drawRoundedRect(QRectF(-28, -38, 56, 76), 3, 3);
        
        p->setBrush(QColor("#78141e")); // 书脊装饰
        p->drawRect(QRectF(13, -38, 8, 76));
    }

    void drawPen(QPainter* p) {
        // 模仿 Python 版的通用钢笔
        p->setPen(Qt::NoPen);
        QLinearGradient bodyGrad(-6, 0, 6, 0);
        bodyGrad.setColorAt(0, QColor("#b43c46"));
        bodyGrad.setColorAt(0.5, QColor("#8c141e"));
        bodyGrad.setColorAt(1, QColor("#3c050a"));
        p->setBrush(bodyGrad);
        p->drawRoundedRect(QRectF(-6, -23, 12, 46), 5, 5);

        // 笔尖
        QPainterPath tipPath;
        tipPath.moveTo(-3, 23);
        tipPath.lineTo(3, 23);
        tipPath.lineTo(0, 37);
        tipPath.closeSubpath();
        p->setBrush(QColor("#f0e6b4"));
        p->drawPath(tipPath);
    }

private slots:
    void updatePhysics() {
        m_time += 0.1;
        
        // 模拟物理惯性与书写抖动
        float targetAngle = -65.0f;
        float speed = m_time * 3.0f;
        float targetX = qSin(speed) * 4.0f;
        float targetY = 2.0f + qCos(speed * 2.0f) * 1.0f;

        float easing = 0.1f;
        m_angle += (targetAngle - m_angle) * easing;
        m_penX += (targetX - m_penX) * easing;
        m_penY += (targetY - m_penY) * easing;
        m_bookY += (-1.0f - m_bookY) * easing;

        update();
        if (m_time > 5.0) {
            m_timer->stop();
            hide();
        }
    }

private:
    QTimer* m_timer;
    float m_time = 0;
    float m_angle = -45;
    float m_penX = 0, m_penY = 0, m_bookY = 0;
};

#endif // WRITINGANIMATION_H
