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
    QRect cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + 3));
    QRect textRect = QRect(option.rect.left() + 3,
                           option.rect.bottom() - textHeight,
                           option.rect.width() - 6,
                           textHeight);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 圆角裁剪（仅作用于卡片）
    QPainterPath clipPath;
    clipPath.addRoundedRect(cardRect, 6, 6);
    painter->setClipPath(clipPath);

    bool hasThumb = index.data(Qt::UserRole + 1).toBool();
    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();

    if (hasThumb && !thumb.isNull()) {
        QPixmap scaled = thumb.scaled(cardRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = cardRect.center().x() - scaled.width() / 2;
        int y = cardRect.center().y() - scaled.height() / 2;
        painter->drawPixmap(x, y, scaled);
    } else {
        painter->fillRect(cardRect, QColor("#2D2D2D"));
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QPoint center = cardRect.center();
        if (!icon.isNull())
            icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }

    // ② 选中高亮叠加层（在裁剪区内，仅覆盖卡片）
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(cardRect, QColor(255, 140, 0, 50)); // 半透明橙色蒙版
    }

    painter->restore(); // ← 释放裁剪区（与上方 save 对应）

    // ③ 选中边框（在裁剪区外绘制，确保完整显示）
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (option.state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor("#FF8C00"), 2)); // 明亮橙色边框
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 6, 6);
    }

    // ④ 文件名（卡片下方，完全不受裁剪影响）
    painter->setPen(option.state & QStyle::State_Selected
                    ? QColor("#FF8C00") : QColor("#C8C8C8"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
        option.fontMetrics.elidedText(
            index.data(Qt::DisplayRole).toString(),
            Qt::ElideMiddle, textRect.width()));

    painter->restore(); // ← 与第二个 save 对应
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
