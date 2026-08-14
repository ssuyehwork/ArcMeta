#ifndef TOOLTIPOVERLAY_H
#define TOOLTIPOVERLAY_H

#include <QWidget>
#include <QToolTip>
#include <QCursor>
#include <QPointer>
#include <QThread>
#include <QMetaObject>

// ----------------------------------------------------------------------------
// ToolTipOverlay: 全局统一的 Tooltip 代理
// [RESTORE] 按照用户指令，严格还原为“旧版本-2”逻辑：
// 1. 废除所有自定义渲染逻辑，直接使用 Qt 原生 QToolTip。
// 2. 通过 white-space:nowrap 强制解决复制内容时的截断问题。
// 3. 保持单例接口以兼容现有代码。
// ----------------------------------------------------------------------------
class ToolTipOverlay : public QWidget {
    Q_OBJECT
public:
    enum Priority {
        PassivePriority = 0,
        ActivePriority = 1
    };

    static ToolTipOverlay* instance() {
        static QPointer<ToolTipOverlay> inst;
        if (!inst) {
            inst = new ToolTipOverlay();
        }
        return inst;
    }

    // 核心逻辑：还原为旧版本-2 的原生包装模式
    void showText(const QPoint& globalPos, const QString& text, int timeout = 700, const QColor& /*borderColor*/ = QColor("#B0B0B0"), Priority /*priority*/ = PassivePriority) {
        if (thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(this, [=, this]() { showText(globalPos, text, timeout); });
            return;
        }

        if (text.isEmpty()) {
            QToolTip::hideText();
            return;
        }

        // [OLD VERSION 2 RESTORE] 强制包装 HTML，使用 white-space:nowrap 杜绝截断
        QString wrappedText;
        if (text.startsWith("<html>")) {
            wrappedText = text;
        } else {
            // 严格对齐旧版本-2 StringUtils::wrapToolTip 逻辑
            wrappedText = QString("<html><span style='white-space:nowrap; color:#EEEEEE;'>%1</span></html>")
                          .arg(text.contains("<") ? text : text.toHtmlEscaped());
        }

        // 使用原生 QToolTip 渲染，解决所有位置偏移和渲染补丁带来的傻逼逻辑
        QToolTip::showText(globalPos + QPoint(15, 15), wrappedText, nullptr, {}, timeout);
    }

    static void hideTip() {
        QToolTip::hideText();
    }

protected:
    explicit ToolTipOverlay() : QWidget(nullptr) {
        setObjectName("ToolTipOverlay代理");
    }
};

#endif // TOOLTIPOVERLAY_H
