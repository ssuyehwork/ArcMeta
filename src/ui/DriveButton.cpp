#include "DriveButton.h"
#include <QPainter>
#include <QMouseEvent>
#include <QFileInfo>
#include <QDir>
#include "UiHelper.h"
#include "ToolTipOverlay.h"

namespace ArcMeta {

DriveButton::DriveButton(const QString& driveLetter, QWidget* parent)
    : QPushButton(parent), m_driveLetter(driveLetter) {
    setFixedSize(60, 28);
    setCursor(Qt::PointingHandCursor);
    
    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &DriveButton::updateAnimation);
}

void DriveButton::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    
    if (m_state == Running) {
        startRotation();
    } else {
        stopRotation();
    }
    update();
}

void DriveButton::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect rect = this->rect().adjusted(1, 1, -1, -1);
    
    // 1. 背景绘制 (严格对齐 Plan-108 视觉规范)
    QColor bgColor;
    QColor borderColor;
    QColor textColor = Style::TextMain;

    switch (m_state) {
        case Inactive:
            bgColor = QColor("#333333");
            borderColor = QColor("#444444");
            textColor = Style::TextDim;
            break;
        case Active:
        case Running:
            bgColor = Style::PrimaryBlue;
            borderColor = Style::PrimaryBlue.lighter(110);
            break;
        case Paused:
            bgColor = QColor("#555555");
            borderColor = QColor("#666666");
            break;
    }

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect, 4, 4);

    // 2. 盘符文字
    painter.setPen(textColor);
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(rect.adjusted(5, 0, -20, 0), Qt::AlignCenter, m_driveLetter);

    // 3. 状态图标绘制
    QRect iconRect(rect.right() - 22, rect.top() + (rect.height() - 16) / 2, 16, 16);
    
    if (m_state == Running) {
        // 旋转绘制 refresh 图标
        painter.save();
        painter.translate(iconRect.center());
        painter.rotate(m_rotationAngle);
        painter.translate(-iconRect.center());
        QPixmap pix = UiHelper::getPixmap("refresh", QSize(16, 16), Qt::white);
        painter.drawPixmap(iconRect, pix);
        painter.restore();
    } else if (m_state == Paused) {
        // 绘制暂停图标
        QIcon pauseIcon = UiHelper::getIcon("pause", Qt::white, 16);
        pauseIcon.paint(&painter, iconRect);
    } else if (m_state == Active) {
        // 激活态待机显示打勾 (或空，暂定打勾以示激活)
        QIcon checkIcon = UiHelper::getIcon("check", Qt::white, 14);
        checkIcon.paint(&painter, iconRect);
    }
}

void DriveButton::mousePressEvent(QMouseEvent* event) {
    QPushButton::mousePressEvent(event);
}

void DriveButton::mouseReleaseEvent(QMouseEvent* event) {
    QPushButton::mouseReleaseEvent(event);
}

void DriveButton::updateAnimation() {
    m_rotationAngle = (m_rotationAngle + 10) % 360;
    update();
}

void DriveButton::startRotation() {
    if (!m_animationTimer->isActive()) {
        m_animationTimer->start(30);
    }
}

void DriveButton::stopRotation() {
    m_animationTimer->stop();
    m_rotationAngle = 0;
}

FolderButton::FolderButton(const QString& folderPath, QWidget* parent)
    : QPushButton(parent), m_folderPath(folderPath) {
    setFixedSize(28, 28);
    setCursor(Qt::PointingHandCursor);
    m_folderName = QFileInfo(folderPath).fileName();
    if (m_folderName.isEmpty()) {
        m_folderName = folderPath;
    }
}

void FolderButton::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    QColor bgColor;
    QColor borderColor;

    // Hover 样式对齐 5.4 标题栏按钮规范，且拒绝使用 rgba 蒙版
    if (underMouse()) {
        if (isDown()) {
            bgColor = QColor("#4E4E52"); // Style::PressedBackground
        } else {
            bgColor = QColor("#3E3E42"); // Style::HoverBackground
        }
        borderColor = QColor("#555555");
    } else {
        bgColor = QColor("#333333");
        borderColor = QColor("#444444");
    }

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(r, 4, 4);

    // 绘制 folder 矢量图标 (SvgIcons)
    QPixmap pix = UiHelper::getPixmap("folder", QSize(16, 16), Style::TextMain);
    QRect iconRect(r.left() + (r.width() - 16) / 2, r.top() + (r.height() - 16) / 2, 16, 16);
    painter.drawPixmap(iconRect, pix);
}

void FolderButton::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event);
    // 悬停时通过全局 ToolTipOverlay 渲染文件夹名称与绝对路径，避免使用原生 ToolTip
    QString tipText = QString("<b>%1</b><br><span style='color:#888888;'>%2</span>")
        .arg(m_folderName)
        .arg(QDir::toNativeSeparators(m_folderPath));
    ToolTipOverlay::instance()->showText(mapToGlobal(QPoint(0, height() + 4)), tipText, 0);
}

void FolderButton::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    ToolTipOverlay::hideTip();
}

} // namespace ArcMeta
