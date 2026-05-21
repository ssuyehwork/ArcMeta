#include "ThumbnailDelegate.h"
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include "UiHelper.h"

namespace ArcMeta {

ThumbnailDelegate::ThumbnailDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void ThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int textHeight = 36;
    // 上半部分为图片卡片区域，下半部分留给文字
    QRect cardRect = option.rect.adjusted(2, 2, -2, -textHeight);

    // --- 第一部分：绘制卡片内容 (需要裁剪) ---
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // 设置 6px 圆角裁剪
    QPainterPath clipPath;
    clipPath.addRoundedRect(cardRect, 6, 6);
    painter->setClipPath(clipPath);

    bool hasThumb = index.data(Qt::UserRole + 1).toBool();
    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();

    if (hasThumb && !thumb.isNull()) {
        // 物理实现 Aspect Fill (铺满但不变形)
        // 先按 KeepAspectRatioByExpanding 缩放
        QPixmap scaledThumb = thumb.scaled(cardRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        // 计算居中坐标，利用裁剪实现铺满效果
        int x = cardRect.center().x() - scaledThumb.width() / 2;
        int y = cardRect.center().y() - scaledThumb.height() / 2;
        painter->drawPixmap(x, y, scaledThumb);
    } else {
        // 降级：深色背景 + 文件类型图标居中
        painter->fillRect(cardRect, QColor("#2D2D2D"));
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QPoint center = cardRect.center();
        icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }
    painter->restore(); // 释放裁剪区

    // --- 第二部分：绘制选中状态和文字 (不需要内容裁剪) ---
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 选中态：蓝色圆角边框叠加在卡片区上 (使用 2px 画笔)
    if (option.state & QStyle::State_Selected) {
        QPen pen(QColor("#094771"), 2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 6, 6); // 稍微向内缩进 1px 确保边框完整
    }

    // 文件名标签（物理位于卡片下方的预留区域）
    QString name = index.data(Qt::DisplayRole).toString();
    QRect textRect = option.rect.adjusted(2, option.rect.height() - textHeight, -2, -4);
    painter->setPen(QColor("#D4D4D4"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                      option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width()));
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    const int textHeight = 36;
    // 编辑器精确定位到卡片下方的文字区域
    QRect textRect = option.rect.adjusted(4, option.rect.height() - textHeight, -4, -4);
    editor->setGeometry(textRect);
}

} // namespace ArcMeta
