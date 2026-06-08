#ifndef TOOLTIPOVERLAY_H
#define TOOLTIPOVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QElapsedTimer>
#include <QDebug>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QTimer>
#include <QThread>
#include <QFontMetrics>
#include <QTextDocument>
#include <QPointer>
#include <QPainterPath>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QRectF>

// ----------------------------------------------------------------------------
// ToolTipOverlay: 全局统一的自定义 Tooltip
// [CRITICAL] 本项目严禁使用任何形式的“Windows 系统默认 Tip 样式”！
// [RULE] 1. 杜绝原生内容带来的系统阴影和不透明度。
// [RULE] 2. 所有的 ToolTip 逻辑必须通过此 ToolTipOverlay 渲染。
// [RULE] 3. 此组件必须保持扁平化 (Flat)，严禁添加任何阴影特效。
// ----------------------------------------------------------------------------
class ToolTipOverlay : public QWidget {
    Q_OBJECT
public:
    static ToolTipOverlay* instance() {
        static ToolTipOverlay* inst = nullptr;
        if (!inst) {
            inst = new ToolTipOverlay();
        }
        return inst;
    }

    // [ULTIMATE FIX] 2026-03-xx 架构级加固：强制消除 OS 动画延时，实现 0 延时瞬时显隐
    // [FIX] 2026-03-14 修复剪贴板监听时 ToolTip 显示 2-3 秒的问题
    // 根因：旧的「接力防护」机制在 timer 已运行时拒绝重启计时器，导致后续调用沿用旧 timer 剩余时间。
    // 修复：每次 showText 都重启 m_hideTimer，保证显示时长始终为传入的 timeout 值。
    void showText(const QPoint& globalPos, const QString& text, int timeout = 700, const QColor& borderColor = QColor("#B0B0B0")) {
        // [THREAD SAFE] 强制确保在主线程执行，防止计时器哑火导致的长亮 Bug
        if (thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(this, [=, this]() { showText(globalPos, text, timeout, borderColor); });
            return;
        }

        if (text.isEmpty()) { hide(); return; }
        
        // 1. 强制寿命上限截断 (仅针对正值超时，0 代表永久显示)
        if (timeout > 0) {
            timeout = qBound(500, timeout, 60000); // 允许上限 60 秒
        }

        // [DIAG] 诊断日志：记录每次 showText 的调用时间和状态
        m_diagTimer.start();
        qDebug() << "[ToolTipOverlay::showText] 被调用 | timeout =" << timeout
                 << "| timer已运行 =" << m_hideTimer.isActive()
                 << "| 窗口可见 =" << isVisible()
                 << "| 文本 =" << text.left(40);

        m_currentBorderColor = borderColor;

        // [BLOCKER FIX] 之前的逻辑仅判断 startsWith("<") 极其不稳
        // 现在统一使用标准的 HTML 包装器，并确保内部文字颜色强制覆盖
        QString htmlBody;
        if (text.contains("<") && text.contains(">")) {
            // 如果疑似 HTML，尝试去除可能存在的 body/html 标签（简单处理）
            htmlBody = text;
        } else {
            // 纯文本：进行 HTML 转义并保留换行
            htmlBody = text.toHtmlEscaped().replace("\n", "<br>");
        }

        m_text = QString(
            "<html><head><style>div, p, span, body { color: #EEEEEE !important; }</style></head>"
            "<body style='margin:0; padding:0; color:#EEEEEE; font-family:\"Microsoft YaHei\",\"Segoe UI\",sans-serif;'>"
            "<div style='color:#EEEEEE !important;'>%1</div>"
            "</body></html>"
        ).arg(htmlBody);
        
        m_doc.setHtml(m_text);
        m_doc.setDocumentMargin(0); // 彻底消除文档默认边距，保证边距完全由 pad 决定
        
        // 1. 弹性计算尺寸
        m_doc.setTextWidth(-1); // 先恢复自然宽度
        qreal idealW = m_doc.idealWidth();
        
        if (idealW > 450) {
            m_doc.setTextWidth(450); // 超过限制则强制折行
        } else {
            m_doc.setTextWidth(idealW); // 否则保持内容宽度
        }
        
        QSize textSize = m_doc.size().toSize();
        
        int padX = 12; 
        int padY = 8;
        
        int w = textSize.width() + padX * 2;
        int h = textSize.height() + padY * 2;
        
        // 最小宽高限制
        w = qMax(w, 40);
        h = qMax(h, 24);
        
        resize(w, h);
        
        // 位置计算 (视觉偏移 15, 15)
        QPoint pos = globalPos + QPoint(15, 15);
        
        // 边缘检测
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
        
        move(pos);
        show();
        raise();
        update();

        // 自动隐藏逻辑：每次都重启计时器，保证最新的 timeout 生效
        if (timeout > 0) {
            m_hideTimer.start(timeout);
            qDebug() << "[ToolTipOverlay] Timer 已启动/重启, timeout =" << timeout << "ms";
        } else {
            m_hideTimer.stop();
        }
    }

    static void hideTip() {
        if (instance()) instance()->hide();
    }

protected:
    explicit ToolTipOverlay() : QWidget(nullptr) {
        // [CRITICAL] 2026-03-xx 架构级变更：
        // 彻底弃用 Qt::ToolTip，因为 OS 会为其强制附加淡出动画导致 2-3 秒的视觉残留。
        // 改用 Qt::Window + Qt::WindowDoesNotAcceptFocus 实现物理级 0 延时显隐。
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | 
                      Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
        setObjectName("ToolTipOverlay");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        
        m_doc.setUndoRedoEnabled(false);
        // [ULTIMATE FIX] 强制锁定调色板颜色，防止继承自全局黑色的 QSS。
        QPalette pal = palette();
        pal.setColor(QPalette::WindowText, QColor("#EEEEEE"));
        pal.setColor(QPalette::Text, QColor("#EEEEEE"));
        pal.setColor(QPalette::ButtonText, QColor("#EEEEEE"));
        setPalette(pal);

        // 终极样式兜底：针对所有可能的标签应用颜色，并使用 !important
        m_doc.setDefaultStyleSheet("body, div, p, span, b, i { color: #EEEEEE !important; font-family: 'Microsoft YaHei', 'Segoe UI'; }"); 
        setStyleSheet("QWidget { color: #EEEEEE !important; background: transparent; }");

        QFont f = font();
        f.setPointSize(9);
        m_doc.setDefaultFont(f);

        m_hideTimer.setSingleShot(true);
        connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
            qint64 elapsed = m_diagTimer.isValid() ? m_diagTimer.elapsed() : -1;
            qDebug() << "[ToolTipOverlay] Timer timeout 触发 → hide() | 实际显示时长 =" << elapsed << "ms";
            hide();
        });

        hide();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // 彻底去阴影，保持扁平矩形风格
        // 1 像素物理偏移校准位
        QRectF rectF(0.5, 0.5, width() - 1, height() - 1);
        
        // 背景色: #2B2B2B
        // 边框色: 动态传入, 默认 #B0B0B0, 宽度 1px
        p.setPen(QPen(m_currentBorderColor, 1));
        p.setBrush(QColor("#2B2B2B"));
        p.drawRoundedRect(rectF, 4, 4);
        
        // 绘制内容预览
        p.save();
        p.translate(12, 8); // Padding Offset
        p.setPen(QColor("#EEEEEE")); // 备用画笔颜色
        m_doc.drawContents(&p);
        p.restore();
    }

private:
    QString m_text;
    QTextDocument m_doc;
    QTimer m_hideTimer;
    QColor m_currentBorderColor = QColor("#B0B0B0");
    QElapsedTimer m_diagTimer; // [DIAG] 用于测量实际显示时长
};

#endif // TOOLTIPOVERLAY_H
