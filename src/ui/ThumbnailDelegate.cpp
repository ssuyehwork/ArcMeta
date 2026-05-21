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
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    QRect rect = option.rect.adjusted(2, 2, -2, -2); // 留出卡片间隙

    // 圆角路径裁剪
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, 6, 6);
    painter->setClipPath(clipPath);

    bool hasThumb = index.data(Qt::UserRole + 1).toBool();
    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();

    if (hasThumb && !thumb.isNull()) {
        // 缩略图铺满卡片
        painter->drawPixmap(rect, thumb.scaled(rect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        // 降级：深色背景 + 文件类型图标居中
        painter->fillRect(rect, QColor("#2D2D2D"));
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QSize iconSize(48, 48);
        QPoint center = rect.center();
        icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }

    // 选中态：蓝色圆角边框叠加
    if (option.state & QStyle::State_Selected) {
        painter->setClipping(false);
        QPen pen(QColor("#094771"), 2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect, 6, 6);
    }

    painter->restore();

    // 文件名标签（卡片下方）
    QString name = index.data(Qt::DisplayRole).toString();
    QRect textRect = option.rect.adjusted(2, option.rect.height() - 36, -2, -4);
    painter->setPen(QColor("#D4D4D4"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                      option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width()));
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

} // namespace ArcMeta
