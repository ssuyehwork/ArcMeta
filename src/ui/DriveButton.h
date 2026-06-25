#ifndef DRIVEBUTTON_H
#define DRIVEBUTTON_H

#include <QPushButton>
#include <QTimer>
#include <QSvgRenderer>
#include <memory>

namespace ArcMeta {

/**
 * @brief 增强型盘符按钮，支持加载动画 (Plan-97)
 */
class DriveButton : public QPushButton {
    Q_OBJECT
public:
    enum State { Inactive, Active, Running, Paused };

    explicit DriveButton(const QString& letter, QWidget* parent = nullptr);

    void setState(State state);
    State state() const { return m_state; }

    // 兼容旧接口 (Plan-97)
    void setLoading(bool loading);
    bool isLoading() const { return m_isLoading; }
    QString letter() const { return m_letter; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onAnimationTimeout();

private:
    void updateStyle();

    QString m_letter;
    State m_state = Inactive;
    bool m_isLoading = false;
    int m_rotationAngle = 0;
    QTimer* m_animationTimer = nullptr;
    std::unique_ptr<QSvgRenderer> m_refreshRenderer;
    std::unique_ptr<QSvgRenderer> m_pauseRenderer;
};

} // namespace ArcMeta

#endif // DRIVEBUTTON_H
