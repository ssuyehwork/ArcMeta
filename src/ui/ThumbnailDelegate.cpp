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
    const int textHeight = 22;
    QRect thumbRect = rect.adjusted(0, 0, 0, -textHeight);
    QRect textRect = rect.adjusted(4, rect.height() - textHeight, -4, 0);

    // 绘制背景
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

    // 1. 绘制图像/图标
    if (isThumbnail && !pixmap.isNull()) {
        QSize thumbSize = pixmap.size();
        thumbSize.scale(thumbRect.size(), Qt::KeepAspectRatio);
        QRect drawRect(thumbRect.center().x() - thumbSize.width() / 2,
                       thumbRect.center().y() - thumbSize.height() / 2,
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
            if (iconSize.width() > thumbRect.width() * 0.8) iconSize.setWidth(thumbRect.width() * 0.8);
            if (iconSize.height() > thumbRect.height() * 0.8) iconSize.setHeight(thumbRect.height() * 0.8);

            QPoint center = thumbRect.center();
            QRect iconRect(center.x() - iconSize.width() / 2,
                           center.y() - iconSize.height() / 2,
                           iconSize.width(), iconSize.height());
            icon.paint(painter, iconRect);
        }
    }

    // 2. 绘制文件名
    QString fileName = index.data(Qt::DisplayRole).toString();
    painter->setPen(option.state & QStyle::State_Selected ? option.palette.highlightedText().color() : QColor("#D4D4D4"));
    QFont font = option.font;
    font.setPointSizeF(8.5); // 稍微缩小字体以适应紧凑布局
    painter->setFont(font);

    QFontMetrics fm(font);
    QString elidedName = fm.elidedText(fileName, Qt::ElideMiddle, textRect.width());
    painter->drawText(textRect, Qt::AlignCenter, elidedName);

    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

} // namespace ArcMeta
