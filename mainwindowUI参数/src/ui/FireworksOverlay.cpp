#include "FireworksOverlay.h"
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <QRandomGenerator>
#include <cmath>
#include <QDebug>
#include <QSettings>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper to get random double in range [min, max)
static double randomDouble(double min, double max) {
    return min + QRandomGenerator::global()->generateDouble() * (max - min);
}

Particle::Particle() : gravity(0), drag(0.92), size(2.0), decay(4.0), alpha(255.0), age(0), 
    index(0), total(1), rotation(0), spin(0), widthFactor(1.0), phase(0), amp(0) {}

bool Particle::update() {
    if (style == "butterfly") {
        pos += vel;
        vel.setY(vel.y() + gravity);
        vel *= drag;
        pos.setX(pos.x() + std::sin(age * 0.2 + phase) * 0.8);
        alpha -= decay;
        age++;
    } else if (style == "dna") {
        pos.setY(pos.y() + vel.y());
        age++;
        double offset = std::sin((pos.y() * 0.05) + phase) * amp;
        pos.setX(initialPos.x() + offset);
        alpha = (offset > 0) ? 255.0 : 100.0;
        if (age > 60) alpha = 0;
    } else if (style == "lightning") {
        alpha -= decay;
    } else if (style == "confetti") {
        pos += vel;
        vel.setY(vel.y() + gravity);
        vel *= drag;
        rotation += spin;
        widthFactor = std::abs(std::cos(rotation));
        alpha -= 2.0;
    } else if (style == "void") {
        if (mode == "suck") {
            pos += vel;
            double dist = std::sqrt(std::pow(pos.x() - initialPos.x(), 2) + std::pow(pos.y() - initialPos.y(), 2));
            if (dist < 5) {
                mode = "boom";
                double angle = randomDouble(0, M_PI * 2);
                double speed = randomDouble(2.0, 8.0);
                vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
                color = Qt::white;
            }
        } else {
            pos += vel;
            alpha -= 5.0;
        }
    } else if (style == "phoenix") {
        pos += vel;
        vel.setY(vel.y() + gravity);
        vel *= drag;
        if (age > 10 && color.green() > 5) {
            color.setGreen(color.green() - 5);
        }
        alpha -= decay;
        age++;
    } else {
        pos += vel;
        vel.setY(vel.y() + gravity);
        vel *= drag;
        alpha -= decay;
    }
    return alpha > 0;
}

FireworksOverlay* FireworksOverlay::m_instance = nullptr;

FireworksOverlay::FireworksOverlay(QWidget* parent) : QWidget(parent) {
    // 增加 Qt::WindowDoesNotAcceptFocus 以减少 DWM 交互开销
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &FireworksOverlay::animate);

    // [NITPICK FIX] 缓存屏幕几何信息
    updateTotalRect();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &FireworksOverlay::updateTotalRect);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &FireworksOverlay::updateTotalRect);
    
    // 2026-03-xx 按照用户要求，解决任务栏闪烁：
    // 初始化即显示并保持，通过不重绘实现逻辑隐藏，避免频繁显隐导致的 DWM 重排和任务栏刷新。
    show();
}

FireworksOverlay* FireworksOverlay::instance() {
    if (!m_instance) {
        m_instance = new FireworksOverlay();
    }
    return m_instance;
}

void FireworksOverlay::explode(const QPoint& pos) {
    // 2026-03-xx 按照用户要求，检查特效开关
    QSettings gs("RapidNotes", "General");
    if (!gs.value("showFireworks", true).toBool()) {
        return;
    }

    // [NITPICK FIX] 使用缓存的屏幕几何信息
    if (geometry() != m_totalRect) {
        setGeometry(m_totalRect);
    }
    
    // show(); // 不再调用 show()，窗口已在构造函数中常驻显示
    QPoint lp = mapFromGlobal(pos);

    QStringList styles = {"neon", "gold", "butterfly", "quantum", "heart", "galaxy", "frozen", "phoenix", 
                          "matrix", "dna", "lightning", "void", "confetti", "chaos"};
    QString style = styles.at(QRandomGenerator::global()->bounded(styles.size()));

    int count = 40;
    if (style == "matrix") count = 15;
    else if (style == "dna" || style == "lightning" || style == "butterfly") count = 30;
    else if (style == "heart" || style == "galaxy") count = 60;

    for (int i = 0; i < count; ++i) {
        Particle p;
        initParticle(p, lp, style, i, count);
        m_particles.append(p);
    }

    if (!m_timer->isActive()) {
        m_timer->start(16);
    }
}

void FireworksOverlay::initParticle(Particle& p, const QPoint& pos, const QString& style, int index, int total) {
    p.pos = pos;
    p.initialPos = pos;
    p.style = style;
    p.index = index;
    p.total = total;
    
    if (style == "butterfly") {
        double angle = randomDouble(0, M_PI * 2);
        double speed = randomDouble(1.0, 3.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity = 0.01;
        p.drag = 0.96;
        p.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 220, 255);
        p.size = randomDouble(3.0, 5.0);
        p.decay = 2.0;
        p.phase = randomDouble(0, M_PI);
    } else if (style == "matrix") {
        static QString chars = "01COPYX";
        p.character = chars.at(QRandomGenerator::global()->bounded(chars.length()));
        p.vel = QPointF(0, randomDouble(3.0, 6.0));
        p.color = QColor(0, 255, 70);
        p.size = QRandomGenerator::global()->bounded(8, 12);
        p.decay = 5.0;
    } else if (style == "dna") {
        p.vel = QPointF(0, -randomDouble(1.0, 3.0));
        p.phase = (double(index) / total) * 4 * M_PI;
        p.amp = randomDouble(10.0, 15.0);
        p.decay = 3.0;
        p.color = (index % 2 == 0) ? QColor(0, 200, 255) : QColor(255, 0, 150);
    } else if (style == "lightning") {
        double angle = randomDouble(0, M_PI * 2);
        double dist = randomDouble(20.0, 60.0);
        QPointF target(pos.x() + std::cos(angle) * dist, pos.y() + std::sin(angle) * dist);
        int steps = 4;
        for (int i = 0; i < steps; ++i) {
            double t = double(i + 1) / steps;
            QPointF next(pos.x() + (target.x() - pos.x()) * t + randomDouble(-10.0, 10.0),
                         pos.y() + (target.y() - pos.y()) * t + randomDouble(-10.0, 10.0));
            p.lightningPoints.append(next);
        }
        p.color = QColor(220, 220, 255);
        p.decay = 20.0;
    } else if (style == "confetti") {
        double angle = randomDouble(0, M_PI * 2);
        double speed = randomDouble(2.0, 6.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity = 0.2;
        p.drag = 0.92;
        p.spin = randomDouble(-0.2, 0.2);
        p.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 200, 255);
        p.size = randomDouble(4.0, 7.0);
    } else if (style == "void") {
        double angle = randomDouble(0, M_PI * 2);
        double dist = randomDouble(40.0, 80.0);
        p.pos = QPointF(pos.x() + std::cos(angle) * dist, pos.y() + std::sin(angle) * dist);
        p.vel = (QPointF(pos) - p.pos) * 0.15;
        p.color = QColor(150, 0, 255);
        p.mode = "suck";
        p.decay = 0;
    } else if (style == "heart") {
        double t = (double(index) / total) * 2 * M_PI;
        double scale = randomDouble(1.0, 1.8);
        p.vel = QPointF((16 * std::pow(std::sin(t), 3)) * 0.1 * scale,
                        -(13 * std::cos(t) - 5 * std::cos(2*t) - 2 * std::cos(3*t) - std::cos(4*t)) * 0.1 * scale);
        p.gravity = 0.02;
        p.color = QColor(255, 80, 150);
        p.decay = 3.0;
    } else if (style == "galaxy") {
        int arm = index % 3;
        double angle = (arm * 2.09) + (double(index) / total) + randomDouble(-0.2, 0.2);
        double speed = randomDouble(1.0, 3.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.color = QColor::fromHsv(QRandomGenerator::global()->bounded(200, 301), 220, 255);
        p.decay = 4.0;
    } else if (style == "frozen") {
        double angle = randomDouble(0, M_PI * 2);
        double speed = randomDouble(5.0, 12.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity = 0.05;
        p.drag = 0.80;
        p.color = QColor(200, 255, 255);
        p.decay = 5.0;
    } else if (style == "phoenix") {
        double angle = randomDouble(M_PI + 0.5, 2 * M_PI - 0.5);
        double speed = randomDouble(1.0, 4.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity = -0.1;
        p.color = QColor(255, int(randomDouble(150, 256)), 50);
        p.decay = 4.0;
    } else if (style == "chaos") {
        p.vel = QPointF(randomDouble(-2.0, 2.0), randomDouble(-2.0, 2.0));
        p.drag = 0.98;
        p.color = QColor(255, 50, 50);
        p.decay = 6.0;
    } else {
        double angle = randomDouble(0, M_PI * 2);
        double speed = randomDouble(1.0, 5.0);
        p.vel = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity = 0.15;
        if (style == "gold") {
            p.color = QColor(255, 235, 100);
            p.gravity = 0.25;
        } else {
            p.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 220, 255);
        }
        if (style == "quantum") {
            p.decay = 5.0;
        }
    }
}

void FireworksOverlay::updateTotalRect() {
    QRect totalRect;
    for (QScreen* screen : QGuiApplication::screens()) {
        totalRect = totalRect.united(screen->geometry());
    }
    m_totalRect = totalRect;
    if (isVisible()) {
        setGeometry(m_totalRect);
    }
}

void FireworksOverlay::animate() {
    if (m_particles.isEmpty()) {
        m_timer->stop();
        // hide(); // 按照用户要求，特效结束不再调用 hide() 以防止任务栏闪烁
        update(); // 触发最后一次重绘以清除残余
        return;
    }
    
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        if (!m_particles[i].update()) {
            m_particles.removeAt(i);
        }
    }
    update();
}

void FireworksOverlay::paintEvent(QPaintEvent* event) {
    if (m_particles.isEmpty()) return;
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    
    for (const Particle& pt : m_particles) {
        int alphaVal = int(pt.alpha);
        // Shimmer logic
        if (pt.style != "matrix") {
            double flicker = randomDouble(0.6, 1.0);
            alphaVal = int(pt.alpha * flicker);
        }
        
        QColor c = pt.color;
        c.setAlpha(std::max(0, std::min(255, alphaVal)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(c);
        
        if (pt.style == "butterfly") {
            double flap = std::abs(std::sin(pt.age * 0.3 + pt.phase));
            painter.save();
            painter.translate(pt.pos);
            double angle = std::atan2(pt.vel.y(), pt.vel.x());
            painter.rotate(angle * 180.0 / M_PI + 90);
            double w = pt.size * flap;
            double h = pt.size;
            painter.drawEllipse(QPointF(-w, 0), w, h);
            painter.drawEllipse(QPointF(w, 0), w, h);
            painter.restore();
        } else if (pt.style == "matrix") {
            painter.setPen(c);
            QFont f("Consolas", int(pt.size));
            f.setBold(true);
            painter.setFont(f);
            painter.drawText(pt.pos, QString(pt.character));
        } else if (pt.style == "lightning") {
            painter.setPen(QPen(c, 1.5));
            QPainterPath path;
            path.moveTo(pt.initialPos);
            for (const QPointF& pnt : pt.lightningPoints) {
                path.lineTo(pnt);
            }
            painter.drawPath(path);
        } else if (pt.style == "confetti") {
            painter.save();
            painter.translate(pt.pos);
            painter.rotate(pt.rotation * 180.0 / M_PI);
            double w = 6 * pt.widthFactor;
            double h = 10;
            painter.drawRect(QRectF(-w / 2, -h / 2, w, h));
            painter.restore();
        } else if (pt.style == "quantum") {
            double s = pt.size * (pt.alpha / 255.0);
            painter.drawRect(QRectF(pt.pos.x() - s / 2, pt.pos.y() - s / 2, s, s));
        } else if (pt.style == "gold") {
            painter.setPen(QPen(c, pt.size));
            painter.drawLine(pt.pos, pt.pos - pt.vel);
        } else {
            painter.drawEllipse(pt.pos, pt.size, pt.size);
        }
    }
}
