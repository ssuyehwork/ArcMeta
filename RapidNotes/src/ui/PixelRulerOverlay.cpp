#include "PixelRulerOverlay.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "../core/DatabaseManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QFontMetrics>
#include <cmath>

PixelRulerOverlay::PixelRulerOverlay(QWidget* parent) : QWidget(nullptr) {
    setObjectName("PixelRulerOverlay");
    // [CRITICAL] 核心架构修复：作为顶级窗口，不使用 grabMouse 以允许与子部件 m_toolbar 交互
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    
    QRect totalRect;
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        QRect geom = screen->geometry();
        totalRect = totalRect.united(geom);
        ScreenCapture cap;
        cap.geometry = geom;
        cap.dpr = screen->devicePixelRatio();
        cap.image = screen->grabWindow(0, 0, 0, geom.width(), geom.height()).toImage();
        m_captures.append(cap);
    }
    setGeometry(totalRect);

    initToolbar();
    setMode(Spacing);
}

PixelRulerOverlay::~PixelRulerOverlay() {
    if (m_toolbar) { m_toolbar->close(); m_toolbar->deleteLater(); }
}

bool PixelRulerOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 700);
            return true;
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PixelRulerOverlay::initToolbar() {
    // 将工具栏作为本窗体的子部件，确保它在最顶层且可交互
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName("rulerToolbar");
    m_toolbar->setStyleSheet(
        "QFrame#rulerToolbar { background: #1e1e1e; border-radius: 8px; border: 1px solid #444; }"
        "QPushButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 8px; }"
        "QPushButton:hover { background: #333; border: 1px solid #555; }"
        "QPushButton:checked { background: #007ACC; border: 1px solid #007ACC; }"
    );
    auto* l = new QHBoxLayout(m_toolbar);
    l->setContentsMargins(8, 4, 8, 4);
    l->setSpacing(8);

    auto addBtn = [&](const QString& icon, const QString& tip, Mode m, int key) {
        auto* btn = new QPushButton();
        btn->setAutoDefault(false);
        btn->setIcon(IconHelper::getIcon(icon, "#FFFFFF"));
        btn->setIconSize(QSize(20, 20));
        btn->setCheckable(true);
        btn->setProperty("tooltipText", QString("%1 (数字键 %2)").arg(tip).arg(key));
        btn->installEventFilter(this);
        connect(btn, &QPushButton::clicked, [this, m, btn](){
            for(auto* b : m_toolbar->findChildren<QPushButton*>()) b->setChecked(false);
            btn->setChecked(true);
            setMode(m);
        });
        l->addWidget(btn);
        if (m == Spacing) btn->setChecked(true);
        return btn;
    };

    addBtn("ruler_bounds", "边界测量", Bounds, 1);
    addBtn("ruler_spacing", "十字测量", Spacing, 2);
    addBtn("ruler_hor", "水平测量", Horizontal, 3);
    addBtn("ruler_ver", "垂直测量", Vertical, 4);

    auto* btnClose = new QPushButton();
    btnClose->setAutoDefault(false);
    btnClose->setIcon(IconHelper::getIcon("close", "#E81123"));
    btnClose->setIconSize(QSize(20, 20));
    connect(btnClose, &QPushButton::clicked, this, &QWidget::close);
    l->addWidget(btnClose);

    m_toolbar->adjustSize();
    m_toolbar->move((width() - m_toolbar->width()) / 2, 40);
    m_toolbar->show();
}

void PixelRulerOverlay::setMode(Mode m) {
    m_mode = m;
    m_startPoint = QPoint();
    update();
}

void PixelRulerOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // 背景填充极低透明度，确保捕获鼠标移动
    p.fillRect(rect(), QColor(0, 0, 0, 1));
    p.setRenderHint(QPainter::Antialiasing);

    QPoint cur = mapFromGlobal(QCursor::pos());
    
    if (m_mode == Spacing) {
        drawCrossSpacing(p, cur);
    } else if (m_mode == Horizontal) {
        drawOneWaySpacing(p, cur, true);
    } else if (m_mode == Vertical) {
        drawOneWaySpacing(p, cur, false);
    } else if (m_mode == Bounds) {
        if (!m_startPoint.isNull()) drawBounds(p, m_startPoint, cur);
    }
}

// 绘制十字探测
void PixelRulerOverlay::drawCrossSpacing(QPainter& p, const QPoint& pos) {
    const ScreenCapture* cap = getCapture(mapToGlobal(pos));
    if (!cap) return;

    QPoint relPos = mapToGlobal(pos) - cap->geometry.topLeft();
    int px = relPos.x() * cap->dpr;
    int py = relPos.y() * cap->dpr;

    int left = findEdge(cap->image, px, py, -1, 0) / cap->dpr;
    int right = findEdge(cap->image, px, py, 1, 0) / cap->dpr;
    int top = findEdge(cap->image, px, py, 0, -1) / cap->dpr;
    int bottom = findEdge(cap->image, px, py, 0, 1) / cap->dpr;

    // 使用橙红色实线 (#ff5722)，对标用户提供的设计图
    p.setPen(QPen(QColor(255, 87, 34), 1, Qt::SolidLine));
    p.drawLine(pos.x() - left, pos.y(), pos.x() + right, pos.y());
    p.drawLine(pos.x(), pos.y() - top, pos.x(), pos.y() + bottom);

    // 绘制两端的小圆点 (对标 PowerToys 细节)
    p.setBrush(QColor(255, 87, 34));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPoint(pos.x() - left, pos.y()), 2, 2);
    p.drawEllipse(QPoint(pos.x() + right, pos.y()), 2, 2);
    p.drawEllipse(QPoint(pos.x(), pos.y() - top), 2, 2);
    p.drawEllipse(QPoint(pos.x(), pos.y() + bottom), 2, 2);

    // [CRITICAL] 采用单标签汇总模式，显示 W x H，避免四个标签互相遮挡
    // 偏移位置设在交叉点右下方，避免遮挡准星
    QString text = QString("%1 × %2").arg(left + right).arg(top + bottom);
    drawInfoBox(p, pos + QPoint(60, 30), text);
}

// 绘制单向探测 (水平或垂直)
void PixelRulerOverlay::drawOneWaySpacing(QPainter& p, const QPoint& pos, bool hor) {
    const ScreenCapture* cap = getCapture(mapToGlobal(pos));
    if (!cap) return;

    QPoint relPos = mapToGlobal(pos) - cap->geometry.topLeft();
    int px = relPos.x() * cap->dpr;
    int py = relPos.y() * cap->dpr;

    p.setPen(QPen(QColor(255, 87, 34), 1, Qt::SolidLine));
    if (hor) {
        int left = findEdge(cap->image, px, py, -1, 0) / cap->dpr;
        int right = findEdge(cap->image, px, py, 1, 0) / cap->dpr;
        p.drawLine(pos.x() - left, pos.y(), pos.x() + right, pos.y());
        // 绘制两端截止线
        p.drawLine(pos.x() - left, pos.y() - 10, pos.x() - left, pos.y() + 10);
        p.drawLine(pos.x() + right, pos.y() - 10, pos.x() + right, pos.y() + 10);
        drawLabel(p, pos.x() + (right - left)/2, pos.y() - 20, left + right, true, true);
    } else {
        int top = findEdge(cap->image, px, py, 0, -1) / cap->dpr;
        int bottom = findEdge(cap->image, px, py, 0, 1) / cap->dpr;
        p.drawLine(pos.x(), pos.y() - top, pos.x(), pos.y() + bottom);
        p.drawLine(pos.x() - 10, pos.y() - top, pos.x() + 10, pos.y() - top);
        p.drawLine(pos.x() - 10, pos.y() + bottom, pos.x() + 10, pos.y() + bottom);
        drawLabel(p, pos.x() + 20, pos.y() + (bottom - top)/2, top + bottom, false, true);
    }
}

void PixelRulerOverlay::drawLabel(QPainter& p, int x, int y, int val, bool isHor, bool isFixed) {
    if (val <= 1) return;
    QString text = QString::number(val);
    drawInfoBox(p, QPoint(x, y), text);
}

void PixelRulerOverlay::drawBounds(QPainter& p, const QPoint& s, const QPoint& e) {
    QRect r = QRect(s, e).normalized();
    p.setPen(QPen(Qt::cyan, 2));
    p.setBrush(QColor(0, 255, 255, 30));
    p.drawRect(r);

    QString text = QString("%1 × %2").arg(r.width()).arg(r.height());
    // [CRITICAL] 优化 Tip 位置：不再显示在选取中心，而是显示在选取下方且位于鼠标光标左下角
    QFontMetrics fm(p.font());
    int w = fm.horizontalAdvance(text) + 20;
    int h = 26;

    // 计算位置：右边缘靠近鼠标，且整体在选取区域下方
    int tipX = e.x() - 5 - w / 2;
    int tipY = std::max(r.bottom(), e.y()) + 10 + h / 2;
    
    drawInfoBox(p, QPoint(tipX, tipY), text);
}

void PixelRulerOverlay::drawInfoBox(QPainter& p, const QPoint& pos, const QString& text) {
    QFontMetrics fm(p.font());
    int w = fm.horizontalAdvance(text) + 20;
    int h = 26;
    // 以 pos 为中心绘制
    QRect r(pos.x() - w/2, pos.y() - h/2, w, h);
    
    // 自动边界调整，确保标签不超出屏幕
    if (r.right() > width()) r.moveRight(width() - 10);
    if (r.left() < 0) r.moveLeft(10);
    if (r.bottom() > height()) r.moveBottom(height() - 10);
    if (r.top() < 0) r.moveTop(10);

    // 添加 1 像素深灰色边框
    p.setPen(QPen(QColor(176, 176, 176), 1));
    p.setBrush(QColor(43, 43, 43)); // 移除透明度，改为完全不透明
    p.drawRoundedRect(r, 4, 4);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, text);
}

int PixelRulerOverlay::findEdge(const QImage& img, int x, int y, int dx, int dy) {
    if (!img.rect().contains(x, y)) return 0;
    QColor startColor = img.pixelColor(x, y);
    int dist = 0;
    int curX = x + dx, curY = y + dy;
    while (img.rect().contains(curX, curY)) {
        QColor c = img.pixelColor(curX, curY);
        // 比较颜色差异,大于阈值则认为遇到了边界
        // 阈值 10: RGB 总差异 < 10 视为同色,提高边界检测精度
        if (colorDiff(startColor, c) > 10) break;
        dist++;
        curX += dx;
        curY += dy;
    }
    return dist;
}

int PixelRulerOverlay::colorDiff(const QColor& c1, const QColor& c2) {
    return std::abs(c1.red() - c2.red()) + std::abs(c1.green() - c2.green()) + std::abs(c1.blue() - c2.blue());
}

const PixelRulerOverlay::ScreenCapture* PixelRulerOverlay::getCapture(const QPoint& globalPos) {
    for (const auto& cap : m_captures) if (cap.geometry.contains(globalPos)) return &cap;
    return m_captures.isEmpty() ? nullptr : &m_captures[0];
}

void PixelRulerOverlay::mousePressEvent(QMouseEvent* event) {
    // [CRITICAL] 修正：如果点击在工具栏上，不触发测量逻辑
    if (m_toolbar->geometry().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_startPoint = event->pos();
        
        // 瞬间测量模式，点击即保存
        if (m_mode == Spacing || m_mode == Horizontal || m_mode == Vertical) {
            saveMeasurement(getMeasurementText(event->pos()));
        }
        
        update();
    } else if (event->button() == Qt::RightButton) {
        // [用户修改要求] 拦截右键按下，统一在 Release 中处理取消逻辑，防止事件穿透到第三方应用触发菜单
        event->accept();
    }
}

void PixelRulerOverlay::mouseMoveEvent(QMouseEvent* event) {
    update();
}

void PixelRulerOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !m_startPoint.isNull()) {
        // 边界测量模式，松开即保存
        if (m_mode == Bounds) {
            QRect r = QRect(m_startPoint, event->pos()).normalized();
            if (r.width() > 1 && r.height() > 1) {
                QString val = QString("%1 × %2").arg(r.width()).arg(r.height());
                saveMeasurement(val);
            }
        }
        m_startPoint = QPoint();
        update();
    } else if (event->button() == Qt::RightButton) {
        // [用户修改要求] 右键单击触发放弃任务逻辑，且必须在 Release 时处理以完全拦截点击流，防止穿透
        // 2026-03-xx 核心修复：显式清理大额截图内存，防止内存堆叠
        m_captures.clear();
        close();
        event->accept();
    }
}

void PixelRulerOverlay::contextMenuEvent(QContextMenuEvent* event) {
    // [用户修改要求] 彻底拦截上下文菜单事件，确保在标尺测量时不会弹出系统或第三方菜单
    event->accept();
}

QString PixelRulerOverlay::getMeasurementText(const QPoint& pos) {
    const ScreenCapture* cap = getCapture(mapToGlobal(pos));
    if (!cap) return "";

    QPoint relPos = mapToGlobal(pos) - cap->geometry.topLeft();
    int px = relPos.x() * cap->dpr;
    int py = relPos.y() * cap->dpr;

    if (m_mode == Spacing) {
        int left = findEdge(cap->image, px, py, -1, 0) / cap->dpr;
        int right = findEdge(cap->image, px, py, 1, 0) / cap->dpr;
        int top = findEdge(cap->image, px, py, 0, -1) / cap->dpr;
        int bottom = findEdge(cap->image, px, py, 0, 1) / cap->dpr;
        return QString("%1 × %2").arg(left + right).arg(top + bottom);
    } else if (m_mode == Horizontal) {
        int left = findEdge(cap->image, px, py, -1, 0) / cap->dpr;
        int right = findEdge(cap->image, px, py, 1, 0) / cap->dpr;
        return QString::number(left + right);
    } else if (m_mode == Vertical) {
        int top = findEdge(cap->image, px, py, 0, -1) / cap->dpr;
        int bottom = findEdge(cap->image, px, py, 0, 1) / cap->dpr;
        return QString::number(top + bottom);
    }
    return "";
}

void PixelRulerOverlay::saveMeasurement(const QString& val) {
    if (val.isEmpty()) return;
    
    DatabaseManager::instance().addNoteAsync(
        val,              // 标题改为像素值本身
        val,              // 内容改为像素值本身
        {"标尺", "测量", "像素"},
        "#ff5722", // 使用测量线的橙红色作为笔记卡片主色
        -1,
        "pixel_ruler"
    );
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("[OK] 测量值已存入数据库: %1").arg(val));
}

void PixelRulerOverlay::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Escape) {
        // [MODIFIED] 标尺也是瞬时工具，直接退出
        // 2026-03-xx 核心修复：显式清理大额截图内存
        m_captures.clear();
        close();
    }
    else if (key == Qt::Key_1) setMode(Bounds);
    else if (key == Qt::Key_2) setMode(Spacing);
    else if (key == Qt::Key_3) setMode(Horizontal);
    else if (key == Qt::Key_4) setMode(Vertical);
    
    // 同步工具栏按钮状态
    if (key >= Qt::Key_1 && key <= Qt::Key_4) {
        auto btns = m_toolbar->findChildren<QPushButton*>();
        int idx = key - Qt::Key_1;
        if (idx >= 0 && idx < btns.size()) {
            for(auto* b : btns) b->setChecked(false);
            btns[idx]->setChecked(true);
        }
    }
}
