#include "FloatingBall.h"
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPainterPath>
#include <QtMath>
#include <QRandomGenerator>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QDateTime>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <QSettings>
#include <QApplication>
#include <utility>

FloatingBall::FloatingBall(QWidget* parent) 
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::X11BypassWindowManagerHint) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);
    setFixedSize(120, 120); // 1:1 复刻 Python 版尺寸
    
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &FloatingBall::updatePhysics);
    m_timer->start(16);

    restorePosition();
    
    QSettings settings("RapidNotes", "FloatingBall");
    QString savedSkin = settings.value("skin", "mocha").toString();
    switchSkin(savedSkin);
}

void FloatingBall::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    float cx = width() / 2.0f;
    float cy = height() / 2.0f;

    // 1. 绘制柔和投影 (根据皮肤形状动态适配，带羽化效果)
    painter.save();
    float s = 1.0f - (m_bookY / 25.0f); // 随高度缩放
    float shadowOpacity = 40 * s;
    
    if (m_skinName == "open") {
        // 摊开手稿皮肤：较宽的柔和投影
        float sw = 84, sh = 20;
        QRadialGradient grad(cx, cy + 35, sw/2);
        grad.setColorAt(0, QColor(0, 0, 0, shadowOpacity));
        grad.setColorAt(0.8, QColor(0, 0, 0, shadowOpacity * 0.3));
        grad.setColorAt(1, Qt::transparent);
        painter.setBrush(grad);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(cx - (sw/2)*s, cy + 30, sw*s, sh*s));
    } else {
        // 笔记本皮肤：窄长且极度羽化的投影
        float sw = 48, sh = 12;
        QRadialGradient grad(cx, cy + 42, sw/2);
        grad.setColorAt(0, QColor(0, 0, 0, shadowOpacity));
        grad.setColorAt(0.7, QColor(0, 0, 0, shadowOpacity * 0.4));
        grad.setColorAt(1, Qt::transparent);
        painter.setBrush(grad);
        painter.setPen(Qt::NoPen);
        // 严格限制宽度在本体(56px)以内，杜绝边缘露头
        painter.drawEllipse(QRectF(cx - (sw/2)*s, cy + 38, sw*s, sh*s));
    }
    painter.restore();

    // 2. 绘制粒子
    for (const auto& p : m_particles) {
        QColor c = p.color;
        c.setAlphaF(p.life);
        painter.setBrush(c);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(p.pos, p.size, p.size);
    }

    // 3. 绘制笔记本
    painter.save();
    painter.translate(cx, cy + m_bookY);
    renderBook(&painter, m_skinName, 0); // 在 paintEvent 中只有 y 偏移是 translate 处理的，book 内部绘制居中
    painter.restore();

    // 4. 绘制钢笔
    painter.save();
    // paintEvent 中 pen 的位置偏移已经在 translate 中处理了
    // 但是原始代码是 translate(cx + m_penX, cy + m_penY - 5);
    // renderPen 需要 relative 坐标吗？
    // 让我们保持 renderPen 只负责画笔本身，坐标变换在外部做。
    painter.translate(cx + m_penX, cy + m_penY - 5);
    painter.rotate(m_penAngle);
    renderPen(&painter, m_skinName, 0, 0, 0); // 坐标和旋转已在外部 Transform 中完成
    painter.restore();
}

void FloatingBall::renderBook(QPainter* p, const QString& skinName, float /*bookY*/) {
    // bookY 参数在此场景下其实不需要，因为 painter 已经 translate 了
    // 为了保持静态函数的通用性，我们保留接口
    
    p->setPen(Qt::NoPen);
    if (skinName == "open") {
        float w = 80, h = 50;
        p->rotate(-5);
        QPainterPath path;
        path.moveTo(-w/2, -h/2); path.lineTo(0, -h/2 + 4);
        path.lineTo(w/2, -h/2); path.lineTo(w/2, h/2);
        path.lineTo(0, h/2 + 4); path.lineTo(-w/2, h/2); path.closeSubpath();
        p->setBrush(QColor("#f8f8f5"));
        p->drawPath(path);
        // 中缝阴影
        QLinearGradient grad(-10, 0, 10, 0);
        grad.setColorAt(0, QColor(0,0,0,0)); grad.setColorAt(0.5, QColor(0,0,0,20)); grad.setColorAt(1, QColor(0,0,0,0));
        p->setBrush(grad);
        p->drawRect(QRectF(-5, -h/2+4, 10, h-4));
        // 横线
        p->setPen(QPen(QColor(200, 200, 200), 1));
        for (int y = (int)(-h/2)+15; y < (int)(h/2); y += 7) {
            p->drawLine(int(-w/2+5), y, -5, y+2);
            p->drawLine(5, y+2, int(w/2-5), y);
        }
    } else {
        float w = 56, h = 76;
        if (skinName == "classic") {
            p->setBrush(QColor("#ebebe6"));
            p->drawRoundedRect(QRectF(-w/2+6, -h/2+6, w, h), 3, 3);
            QLinearGradient grad(-w, -h, w, h);
            grad.setColorAt(0, QColor("#3c3c41")); grad.setColorAt(1, QColor("#141419"));
            p->setBrush(grad);
            p->drawRoundedRect(QRectF(-w/2, -h/2, w, h), 3, 3);
            p->setBrush(QColor(10, 10, 10, 200));
            p->drawRect(QRectF(w/2 - 12, -h/2, 6, h));
        } else if (skinName == "royal") {
            p->setBrush(QColor("#f0f0eb"));
            p->drawRoundedRect(QRectF(-w/2+6, -h/2+6, w, h), 2, 2);
            QLinearGradient grad(-w, -h, w, 0);
            grad.setColorAt(0, QColor("#282864")); grad.setColorAt(1, QColor("#0a0a32"));
            p->setBrush(grad);
            p->drawRoundedRect(QRectF(-w/2, -h/2, w, h), 2, 2);
            p->setBrush(QColor(218, 165, 32));
            float c_size = 12;
            QPolygonF poly; poly << QPointF(w/2, -h/2) << QPointF(w/2-c_size, -h/2) << QPointF(w/2, -h/2+c_size);
            p->drawPolygon(poly);
        } else if (skinName == "matcha") {
            p->setBrush(QColor("#fafaf5"));
            p->drawRoundedRect(QRectF(-w/2+5, -h/2+5, w, h), 3, 3);
            QLinearGradient grad(-w, -h, w, h);
            grad.setColorAt(0, QColor("#a0be96")); grad.setColorAt(1, QColor("#64825a"));
            p->setBrush(grad);
            p->drawRoundedRect(QRectF(-w/2, -h/2, w, h), 3, 3);
            p->setBrush(QColor(255, 255, 255, 200));
            p->drawRoundedRect(QRectF(-w/2+10, -20, 34, 15), 2, 2);
        } else { // mocha / default
            p->setBrush(QColor("#f5f0e1"));
            p->drawRoundedRect(QRectF(-w/2+6, -h/2+6, w, h), 3, 3);
            QLinearGradient grad(-w, -h, w, h);
            grad.setColorAt(0, QColor("#5a3c32")); grad.setColorAt(1, QColor("#321e19"));
            p->setBrush(grad);
            p->drawRoundedRect(QRectF(-w/2, -h/2, w, h), 3, 3);
            p->setBrush(QColor(120, 20, 30));
            p->drawRect(QRectF(w/2 - 15, -h/2, 8, h));
        }
    }
}

void FloatingBall::renderPen(QPainter* p, const QString& skinName, float, float, float) {
    p->setPen(Qt::NoPen);
    QColor c_light, c_mid, c_dark;
    if (skinName == "royal") {
        c_light = QColor(60, 60, 70); c_mid = QColor(20, 20, 25); c_dark = QColor(26, 26, 26);
    } else if (skinName == "classic") {
        c_light = QColor(80, 80, 80); c_mid = QColor(30, 30, 30); c_dark = QColor(10, 10, 10);
    } else if (skinName == "matcha") {
        c_light = QColor(255, 255, 250); c_mid = QColor(240, 240, 230); c_dark = QColor(200, 200, 190);
    } else {
        c_light = QColor(180, 60, 70); c_mid = QColor(140, 20, 30); c_dark = QColor(60, 5, 10);
    }

    QLinearGradient bodyGrad(-6, 0, 6, 0);
    bodyGrad.setColorAt(0.0, c_light); bodyGrad.setColorAt(0.5, c_mid); bodyGrad.setColorAt(1.0, c_dark);
    QPainterPath path_body; path_body.addRoundedRect(QRectF(-6, -23, 12, 46), 5, 5);
    p->setBrush(bodyGrad); p->drawPath(path_body);
    
    QPainterPath tipPath;
    tipPath.moveTo(-3, 23); tipPath.lineTo(3, 23); tipPath.lineTo(0, 37); tipPath.closeSubpath();
    QLinearGradient tipGrad(-5, 0, 5, 0);
    tipGrad.setColorAt(0, QColor(240, 230, 180)); tipGrad.setColorAt(1, QColor(190, 170, 100));
    p->setBrush(tipGrad); p->drawPath(tipPath);
    
    p->setBrush(QColor(220, 200, 140)); p->drawRect(QRectF(-6, 19, 12, 4));
    p->setBrush(QColor(210, 190, 130)); p->drawRoundedRect(QRectF(-1.5, -17, 3, 24), 1.5, 1.5);
}

void FloatingBall::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_isDragging = false; // 初始不进入拖拽，等待 move 判定
        m_penY += 3.0f; // 1:1 复刻 Python 按下弹性反馈
        update();
    }
}

void FloatingBall::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (!m_isDragging) {
            // 只有移动距离超过系统设定的拖拽阈值才开始移动
            if ((event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
                m_isDragging = true;
                m_offset = m_pressPos;
            }
        }
        
        if (m_isDragging) {
            QPoint newPos = event->globalPosition().toPoint() - m_offset;
            QScreen* screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
            if (!screen) screen = QGuiApplication::primaryScreen();
            
            if (screen) {
                QRect ag = screen->availableGeometry();
                int x = qBound(ag.left(), newPos.x(), ag.right() - width());
                int y = qBound(ag.top(), newPos.y(), ag.bottom() - height());
                newPos = QPoint(x, y);
            }
            move(newPos);
        }
    }
}

void FloatingBall::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        savePosition();
    }
}

void FloatingBall::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
    }
}

void FloatingBall::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event);
    m_isHovering = true;
}

void FloatingBall::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    m_isHovering = false;
}

void FloatingBall::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
        /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
        "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
        "QMenu::icon { margin-left: 6px; } "
        "QMenu::item:selected { background-color: #4a90e2; color: white; } "
        "QMenu::separator { background-color: #444; height: 1px; margin: 4px 0; }"
    );

    QMenu* skinMenu = menu.addMenu(IconHelper::getIcon("palette", "#aaaaaa", 18), "切换外观");
    skinMenu->setStyleSheet(menu.styleSheet());
    skinMenu->addAction(IconHelper::getIcon("coffee", "#BCAAA4", 18), "摩卡·勃艮第", [this](){ switchSkin("mocha"); });
    skinMenu->addAction(IconHelper::getIcon("grid", "#90A4AE", 18), "经典黑金", [this](){ switchSkin("classic"); });
    skinMenu->addAction(IconHelper::getIcon("book", "#9FA8DA", 18), "皇家蓝", [this](){ switchSkin("royal"); });
    skinMenu->addAction(IconHelper::getIcon("leaf", "#A5D6A7", 18), "抹茶绿", [this](){ switchSkin("matcha"); });
    skinMenu->addAction(IconHelper::getIcon("book_open", "#FFCC80", 18), "摊开手稿", [this](){ switchSkin("open"); });
    skinMenu->addAction("默认天蓝", [this](){ switchSkin("default"); });

    menu.addSeparator();
    // [NEW] 2026-04-xx 按照用户要求：新增隐藏选项，并使用特征青色图标
    menu.addAction(IconHelper::getIcon("eye", "#41F2F2", 18), "隐藏悬浮球", [this](){
        this->hide();
        emit visibilityChanged(false);
    });
    menu.addAction(IconHelper::getIcon("zap", "#aaaaaa", 18), "打开懒人笔记", this, &FloatingBall::requestQuickWindow);
    menu.addAction(IconHelper::getIcon("add", "#aaaaaa", 18), "新建灵感", this, &FloatingBall::requestNewIdea);
    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("power", "#aaaaaa", 18), "退出程序", [](){ qApp->quit(); });
    
    menu.exec(event->globalPos());
}

void FloatingBall::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasText() || event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
        m_isHovering = true;
    } else {
        event->ignore();
    }
}

void FloatingBall::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    m_isHovering = false;
}

void FloatingBall::dropEvent(QDropEvent* event) {
    m_isHovering = false;
    const QMimeData* mime = event->mimeData();
    
    QString itemType = "text";
    QString title;
    QString content;
    QByteArray dataBlob;
    QStringList tags = {"拖拽"};

    // 1. 优先识别 URLs (文件/文件夹/外部链接)
    if (mime->hasUrls()) {
        QList<QUrl> urls = mime->urls();
        QStringList paths;
        for (const QUrl& url : std::as_const(urls)) {
            if (url.isLocalFile()) {
                QString p = url.toLocalFile();
                paths << p;
                if (title.isEmpty()) {
                    QFileInfo info(p);
                    title = info.fileName();
                    itemType = info.isDir() ? "folder" : "file";
                }
            } else {
                paths << url.toString();
                if (title.isEmpty()) {
                    title = "外部链接";
                    itemType = "link";
                }
            }
        }
        content = paths.join(";");
        if (paths.size() > 1) {
            title = QString("批量导入 (%1个文件)").arg(paths.size());
            itemType = "files";
        }
    } 
    // 2. 其次识别纯文本 (智能切分标题)
    else if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        content = mime->text();
        QStringList lines = content.split('\n');
        for (const QString& line : std::as_const(lines)) {
            QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                title = trimmed.left(50);
                break;
            }
        }
        if (title.isEmpty()) title = "拖拽创建数据";
        itemType = "text";
    } 
    // 3. 最后识别图片
    else if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            itemType = "image";
            title = "[拖入图片] " + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            content = "[Image Data]";
        }
    }

    if (!content.isEmpty() || !dataBlob.isEmpty()) {
        // 2026-04-05 按照用户要求，复刻旧版高阶拖拽逻辑，支持多模态数据存储
        DatabaseManager::instance().addNote(title, content, tags, "", -1, itemType, dataBlob);
        
        // 触发物理粒子效果与书写动画
        burstParticles();
        m_isWriting = true;
        m_writeTimer = 0;
        
        event->acceptProposedAction();
    }
}

QIcon FloatingBall::generateBallIcon() {
    QPixmap pixmap(120, 120);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    float cx = 60.0f;
    float cy = 60.0f;
    
    // 静态状态参数 (无动画)
    float bookY = 0.0f;
    float penX = 0.0f;
    float penY = 0.0f;
    float penAngle = -45.0f;
    QString skinName = "mocha";
    
    // 柔和投影 (图标模式保持静态最佳效果)
    painter.save();
    float sw = 48, sh = 12;
    QRadialGradient grad(cx, cy + 42, sw/2);
    grad.setColorAt(0, QColor(0, 0, 0, 35));
    grad.setColorAt(0.7, QColor(0, 0, 0, 15));
    grad.setColorAt(1, Qt::transparent);
    painter.setBrush(grad);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(cx - sw/2, cy + 38, sw, sh));
    painter.restore();
    
    // 笔记本
    painter.save();
    painter.translate(cx, cy + bookY);
    renderBook(&painter, skinName, 0);
    painter.restore();
    
    // 钢笔
    painter.save();
    painter.translate(cx + penX, cy + penY - 5);
    painter.rotate(penAngle);
    renderPen(&painter, skinName, 0, 0, 0);
    painter.restore();
    
    return QIcon(pixmap);
}

void FloatingBall::switchSkin(const QString& name) {
    m_skinName = name;
    
    QSettings settings("RapidNotes", "FloatingBall");
    settings.setValue("skin", name);
    
    update();
}

void FloatingBall::burstParticles() {
    // 逻辑保持
}

void FloatingBall::updatePhysics() {
    m_timeStep += 0.05f;
    
    // 1. 待机呼吸
    float idlePenY = qSin(m_timeStep * 0.5f) * 4.0f;
    float idleBookY = qSin(m_timeStep * 0.5f - 1.0f) * 2.0f;
    
    float targetPenAngle = -45.0f;
    float targetPenX = 0.0f;
    float targetPenY = idlePenY;
    float targetBookY = idleBookY;
    
    // 2. 书写/悬停动画
    if (m_isWriting || m_isHovering) {
        m_writeTimer++;
        targetPenAngle = -65.0f;
        float writeSpeed = m_timeStep * 3.0f;
        targetPenX = qSin(writeSpeed) * 8.0f;
        targetPenY = 5.0f + qCos(writeSpeed * 2.0f) * 2.0f;
        targetBookY = -3.0f;
        
        if (m_isWriting && m_writeTimer > 90) {
            m_isWriting = false;
        }
    }
    
    // 3. 物理平滑
    float easing = 0.1f;
    m_penAngle += (targetPenAngle - m_penAngle) * easing;
    m_penX += (targetPenX - m_penX) * easing;
    m_penY += (targetPenY - m_penY) * easing;
    m_bookY += (targetBookY - m_bookY) * easing;

    updateParticles();
    update();
}

void FloatingBall::updateParticles() {
    if ((m_isWriting || m_isHovering) && m_particles.size() < 15) {
        if (QRandomGenerator::global()->generateDouble() < 0.3) {
            float rad = qDegreesToRadians(m_penAngle);
            float tipLen = 35.0f;
            Particle p;
            p.pos = QPointF(width()/2.0f + m_penX - qSin(rad)*tipLen, height()/2.0f + m_penY + qCos(rad)*tipLen);
            p.velocity = QPointF(QRandomGenerator::global()->generateDouble() - 0.5, QRandomGenerator::global()->generateDouble() + 0.5);
            p.life = 1.0;
            p.size = 1.0f + QRandomGenerator::global()->generateDouble() * 2.0f;
            p.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 150, 255);
            m_particles.append(p);
        }
    }
    for (int i = 0; i < m_particles.size(); ++i) {
        m_particles[i].pos += m_particles[i].velocity;
        m_particles[i].life -= 0.03;
        m_particles[i].size *= 0.96f;
        if (m_particles[i].life <= 0) {
            m_particles.removeAt(i);
            --i;
        }
    }
}

void FloatingBall::savePosition() {
    QSettings settings("RapidNotes", "FloatingBall");
    settings.setValue("pos", pos());
}

void FloatingBall::restorePosition() {
    QSettings settings("RapidNotes", "FloatingBall");
    if (settings.contains("pos")) {
        QPoint savedPos = settings.value("pos").toPoint();
        QScreen* screen = QGuiApplication::screenAt(savedPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        
        if (screen) {
            QRect ag = screen->availableGeometry();
            int x = qBound(ag.left(), savedPos.x(), ag.right() - width());
            int y = qBound(ag.top(), savedPos.y(), ag.bottom() - height());
            move(x, y);
        } else {
            move(savedPos);
        }
    } else {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect ag = screen->availableGeometry();
            move(ag.right() - 150, ag.top() + ag.height() / 2 - height() / 2);
        }
    }
}