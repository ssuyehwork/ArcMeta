#include "ToolTipOverlay.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QTimer>

namespace ArcMeta {

ToolTipOverlay::ToolTipOverlay() : QWidget(nullptr) {
    // [CRITICAL] 彻底弃用 Qt::ToolTip，防止 OS 动画残留
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | 
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
    setObjectName("ToolTipOverlay");

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    // 2026-06-xx 物理修复：通过原生 API 实现置顶，避免标志位导致的重建问题
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this]() {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    });
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
#endif
    
    m_doc.setUndoRedoEnabled(false);
    // [ULTIMATE FIX] 强制锁定调色板颜色
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, QColor("#EEEEEE"));
    pal.setColor(QPalette::Text, QColor("#EEEEEE"));
    pal.setColor(QPalette::ButtonText, QColor("#EEEEEE"));
    setPalette(pal);

    m_doc.setDefaultStyleSheet("body, div, p, span, b, i { color: #EEEEEE !important; font-family: 'Microsoft YaHei', 'Segoe UI'; }"); 
    setStyleSheet("QWidget { color: #EEEEEE !important; background: transparent; }");

    QFont f = font();
    f.setPointSize(9);
    m_doc.setDefaultFont(f);

    m_fadeAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnim->setDuration(150);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &ToolTipOverlay::fadeOutAndHide);

    // 初始静默隐藏，等待 MainWindow 的 showEvent 触发真正有效的 GPU 预热
    hide();
}

void ToolTipOverlay::fadeOutAndHide() {
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(windowOpacity());
    m_fadeAnim->setEndValue(0.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        QWidget::hide();
        setWindowOpacity(1.0); // 隐藏后重置不透明度以兼容非动画弹出
    });
    m_fadeAnim->start();
}

void ToolTipOverlay::showText(const QPoint& globalPos, const QString& text, int timeout, const QColor& borderColor, bool exactPosition, const QColor& backgroundColor) {
    // [THREAD SAFE] 强制确保在主线程执行
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this, globalPos, text, timeout, borderColor, exactPosition, backgroundColor]() { 
            showText(globalPos, text, timeout, borderColor, exactPosition, backgroundColor); 
        });
        return;
    }

    m_currentBorderColor = borderColor;
    m_currentBackgroundColor = backgroundColor;

    if (timeout > 0) {
        timeout = qBound(500, timeout, 60000); 
    }

    int w = 40;
    int h = 24;

    if (text.isEmpty()) {
        // 2026-xx-xx 按照用户最新指令：当 text 为空时，表明我们处于纯色块（颜色气泡）模式。
        // 我们不解析文档或文本，而是设置成固定 60x24px 大小的纯色色块指示器！
        m_text = "";
        m_doc.clear();
        w = 60;
        h = 24;
    } else {
        // 2026-05-20 性能优化：内容脏检查，防止鼠标在按钮内部微动导致的重复渲染卡顿
        if (isVisible() && m_text == text && m_currentBorderColor == borderColor && !exactPosition) {
            move(globalPos + QPoint(15, 15));
            return;
        }

        QString htmlBody;
        if (text.contains("<") && text.contains(">")) {
            htmlBody = text;
        } else {
            htmlBody = text.toHtmlEscaped().replace("\n", "<br>");
        }

        m_text = QString(
            "<html><head><style>div, p, span, body { color: #EEEEEE !important; }</style></head>"
            "<body style='margin:0; padding:0; color:#EEEEEE; font-family:\"Microsoft YaHei\",\"Segoe UI\",sans-serif;'>"
            "<div style='color:#EEEEEE !important;'>%1</div>"
            "</body></html>"
        ).arg(htmlBody);
        
        m_doc.setHtml(m_text);
        m_doc.setDocumentMargin(0); 
        
        m_doc.setTextWidth(-1); 
        qreal idealW = m_doc.idealWidth();
        
        if (idealW > 450) {
            m_doc.setTextWidth(450); 
        } else {
            m_doc.setTextWidth(idealW); 
        }
        
        QSize textSize = m_doc.size().toSize();
        
        int padX = 12; 
        int padY = 8;
        
        w = textSize.width() + padX * 2;
        h = textSize.height() + padY * 2;
        
        w = qMax(w, 40);
        h = qMax(h, 24);
    }
    
    resize(w, h);
    
    QPoint pos;
    if (exactPosition) {
        pos = globalPos;
    } else {
        pos = globalPos + QPoint(15, 15);
        QScreen* screen = QGuiApplication::screenAt(globalPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            if (pos.x() + width() > screenGeom.right()) {
                pos.setX(globalPos.x() - width() - 15);
            }
            if (pos.y() + height() > screenGeom.bottom()) {
                pos.setY(globalPos.y() - height() - 15);
            }
        }
    }
    
    move(pos);
    
    // 淡入显示
    m_fadeAnim->stop();
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    setWindowOpacity(0.0);
    show();
    update();

    // 2026-xx-xx 特殊修复：由于 QuickLookWindow 自身是通过 SetWindowPos(..., HWND_TOPMOST, ...) 显示的置顶无边框窗口，
    // 为了防止 ToolTipOverlay 被同样是 TOPMOST 的预览窗口意外遮挡，在调用 show() 后，必须强制性通过原生 WinAPI
    // 重新确立 ToolTipOverlay 的最高优先级置顶秩序，确保提示信息绝对不被遮蔽。
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
#else
    raise();
#endif
    
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();

    if (timeout > 0) {
        m_hideTimer.start(timeout);
    } else {
        // 2026-07-xx 按照 Plan-65：如果 timeout 为 0 或负数，停止计时器以支持持续显示
        m_hideTimer.stop();
    }
}

void ToolTipOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    QRectF rectF(0.5, 0.5, width() - 1, height() - 1);
    
    p.setPen(QPen(m_currentBorderColor, 1));
    p.setBrush(m_currentBackgroundColor);
    // 2026-03-xx 按照用户硬性要求：ToolTip 圆角必须锁定为 2px
    p.drawRoundedRect(rectF, 2, 2);
    
    if (!m_text.isEmpty()) {
        p.save();
        p.translate(12, 8); 
        m_doc.drawContents(&p);
        p.restore();
    }
}

} // namespace ArcMeta
