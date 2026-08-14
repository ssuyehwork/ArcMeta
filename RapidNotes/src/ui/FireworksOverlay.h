#ifndef FIREWORKSOVERLAY_H
#define FIREWORKSOVERLAY_H

#include <QWidget>
#include <QTimer>
#include <QColor>
#include <QList>
#include <QPointF>
#include <QPainterPath>

struct Particle {
    QPointF pos;
    QPointF initialPos;
    QPointF vel;
    double gravity;
    double drag;
    double size;
    double decay;
    QColor color;
    double alpha;
    int age;
    QString style;
    int index;
    int total;
    double rotation;
    double spin;
    QChar character;
    double widthFactor;
    double phase;
    double amp;
    QList<QPointF> lightningPoints;
    QString mode; // for 'void' style

    Particle();
    bool update();
};

class FireworksOverlay : public QWidget {
    Q_OBJECT
public:
    explicit FireworksOverlay(QWidget* parent = nullptr);
    static FireworksOverlay* instance();
    
    void explode(const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void animate();
    void updateTotalRect();

private:
    void initParticle(Particle& p, const QPoint& pos, const QString& style, int index, int total);
    
    QList<Particle> m_particles;
    QTimer* m_timer;
    QRect m_totalRect;
    static FireworksOverlay* m_instance;
};

#endif // FIREWORKSOVERLAY_H
