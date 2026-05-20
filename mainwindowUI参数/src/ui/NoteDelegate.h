#ifndef NOTEDELEGATE_H
#define NOTEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QRegularExpression>
#include "../models/NoteModel.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include <QHelpEvent>

class NoteDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit NoteDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    // 定义卡片高度
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        return QSize(option.rect.width(), 110); // 每个卡片高度 110px
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override {
        if (event && event->type() == QEvent::ToolTip && index.isValid()) {
            QString tip = index.data(Qt::ToolTipRole).toString();
            if (!tip.isEmpty()) {
                // 2026-03-xx 按照用户要求，列表数据 ToolTip 持续时间设为 2 秒 (2000ms)
                ToolTipOverlay::instance()->showText(event->globalPos(), tip, 2000);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 1. 获取数据
        QString title = index.data(NoteModel::TitleRole).toString();
        QString content = index.data(NoteModel::ContentRole).toString();
        QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        bool isPinned = index.data(NoteModel::PinnedRole).toBool();
        
        // 2. 处理选中状态和背景 (更精致的配色与阴影感)
        bool isSelected = (option.state & QStyle::State_Selected);
        
        // 【关键修复】使用 QRectF 并根据笔宽调整，确保 2px 边框完全在 option.rect 内部绘制，消除选中残留伪影
        qreal penWidth = isSelected ? 2.0 : 1.0;
        QRectF rect = QRectF(option.rect).adjusted(penWidth/2.0, penWidth/2.0, -penWidth/2.0, -4.0 - penWidth/2.0);
        
        // 获取笔记自身的颜色标记作为背景
        QString colorHex = index.data(NoteModel::ColorRole).toString();
        QColor noteColor = colorHex.isEmpty() ? QColor("#1a1a1b") : QColor(colorHex);
        
        QColor bgColor = isSelected ? noteColor.lighter(115) : noteColor; 
        QColor borderColor = isSelected ? QColor("#ffffff") : QColor("#333333");
        
        // 绘制卡片背景
        QPainterPath path;
        path.addRoundedRect(rect, 8, 8);
        
        // 模拟阴影
        if (!isSelected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 40));
            painter->drawRoundedRect(rect.translated(0, 2), 8, 8);
        }

        painter->setPen(QPen(borderColor, penWidth));
        painter->setBrush(bgColor);
        painter->drawPath(path);

        // 3. 绘制标题 (加粗，主文本色: 统一设为白色以应对多样背景卡片)
        painter->setPen(Qt::white);
        QFont titleFont("Microsoft YaHei", 10, QFont::Bold);
        painter->setFont(titleFont);
        QRectF titleRect = rect.adjusted(12, 10, -35, -70);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, painter->fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width()));

        // 4. 绘制置顶/星级标识
        if (isPinned) {
            // 2026-03-12 按照用户要求，统一置顶标识颜色为橙色 (#FF551C)，并使用实心图钉图标 (pin_vertical)
            QPixmap pin = IconHelper::getIcon("pin_vertical", "#FF551C", 14).pixmap(14, 14);
            painter->drawPixmap(rect.right() - 25, rect.top() + 12, pin);
        }

        // 5. 绘制内容预览 (强制纯白：确保在任何背景下都有最高清晰度)
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei", 9));
        QRectF contentRect = rect.adjusted(12, 34, -12, -32);
        
        // [PERF] 极致性能优化：严禁在此调用 StringUtils::htmlToPlainText (涉及实例化 QTextDocument 及 HTML 解析)。
        // 改为直接从 Model 获取已缓存的 PlainContentRole，实现渲染阶段的零 CPU 抖动。
        QString cleanContent = index.data(NoteModel::PlainContentRole).toString();
        QString elidedContent = painter->fontMetrics().elidedText(cleanContent, Qt::ElideRight, contentRect.width() * 2);
        painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedContent);

        // 6. 绘制底部元数据栏 (时间图标 + 时间 + 类型标签)
        QRectF bottomRect = rect.adjusted(12, 78, -12, -8);
        
        // 时间 (强制纯白)
        painter->setPen(Qt::white);
        painter->setFont(QFont("Segoe UI", 8));
        QPixmap clock = IconHelper::getIcon("clock", "#ffffff", 12).pixmap(12, 12);
        painter->drawPixmap(bottomRect.left(), bottomRect.top() + (bottomRect.height() - 12) / 2, clock);
        painter->drawText(bottomRect.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, timeStr);

        // 绘制类型图标 (对齐 QuickWindow 风格)
        QIcon typeIcon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!typeIcon.isNull()) {
            int iconSize = 18;
            QRectF iconRect(bottomRect.right() - iconSize - 4, bottomRect.top() + (bottomRect.height() - iconSize) / 2, iconSize, iconSize);
            typeIcon.paint(painter, iconRect.toRect(), Qt::AlignCenter);
        }

        painter->restore();
    }
};

#endif // NOTEDELEGATE_H