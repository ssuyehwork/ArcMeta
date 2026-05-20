#include "ThumbnailDelegate.h"
#include <QPainter>
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

    QRect rect = option.rect;

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(rect, QColor(255, 255, 255, 20));
    }

    QVariant decoration = index.data(Qt::DecorationRole);
    QPixmap pixmap;
    bool isThumbnail = index.data(Qt::UserRole + 1).toBool();

    if (decoration.canConvert<QPixmap>()) {
        pixmap = decoration.value<QPixmap>();
    }

    if (isThumbnail && !pixmap.isNull()) {
        QSize thumbSize = pixmap.size();
        thumbSize.scale(rect.size(), Qt::KeepAspectRatio);
        QRect drawRect(rect.center().x() - thumbSize.width() / 2,
                       rect.center().y() - thumbSize.height() / 2,
                       thumbSize.width(), thumbSize.height());
        painter->drawPixmap(drawRect, pixmap);
    } else {
        QIcon icon;
        if (decoration.canConvert<QIcon>()) {
            icon = decoration.value<QIcon>();
        } else if (decoration.canConvert<QPixmap>()) {
            icon = QIcon(decoration.value<QPixmap>());
        }

        if (!icon.isNull()) {
            QSize iconSize(48, 48);
            if (iconSize.width() > rect.width() * 0.8) iconSize.setWidth(rect.width() * 0.8);
            if (iconSize.height() > rect.height() * 0.8) iconSize.setHeight(rect.height() * 0.8);

            QPoint center = rect.center();
            QRect iconRect(center.x() - iconSize.width() / 2,
                           center.y() - iconSize.height() / 2,
                           iconSize.width(), iconSize.height());
            icon.paint(painter, iconRect);
        }
    }

    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

} // namespace ArcMeta
