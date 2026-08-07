#pragma once
#include <QWidget>
#include <QColor>
#include <QPaintEvent>
#include <QEvent>
#include <QEnterEvent>
#include <QMouseEvent>

namespace ArcMeta {

class ColorPill : public QWidget {
    Q_OBJECT
public:
    explicit ColorPill(const QColor& color, float ratio, QWidget* parent = nullptr);
    void setData(const QColor& color, float ratio);
signals:
    void colorSelected(const QColor& color);
    void requestSetAsPrimary(const QColor& color);
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    QColor m_color;
    float m_ratio;
    bool m_hovered = false;
};

} // namespace ArcMeta
