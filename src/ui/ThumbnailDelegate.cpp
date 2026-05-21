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
    const int textHeight = 30;
    QRect thumbRect = option.rect.adjusted(2, 2, -2, -textHeight - 2); // 缩略图区域（卡片）
    QRect textRect = option.rect.adjusted(2, option.rect.height() - textHeight, -2, -2); // 文本区域（卡片下方）

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // 1. 绘制卡片内容（带圆角裁剪）
    {
        painter->save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbRect, 6, 6);
        painter->setClipPath(clipPath);

        bool hasThumb = index.data(Qt::UserRole + 1).toBool();
        QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();

        if (hasThumb && !thumb.isNull()) {
            // 缩略图铺满卡片 (KeepAspectRatioByExpanding + Center Cropping)
            QSize scaledSize = thumb.size();
            scaledSize.scale(thumbRect.size(), Qt::KeepAspectRatioByExpanding);
            QRect drawRect(thumbRect.center().x() - scaledSize.width() / 2,
                           thumbRect.center().y() - scaledSize.height() / 2,
                           scaledSize.width(), scaledSize.height());
            painter->drawPixmap(drawRect, thumb);
        } else {
            // 降级：深色背景 + 文件类型图标居中
            painter->fillRect(thumbRect, QColor("#2D2D2D"));
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            QSize iconSize(48, 48);
            QPoint center = thumbRect.center();
            icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
        }
        painter->restore();
    }

    // 2. 选中态：3像素蓝色圆角边框叠加 (不裁剪，确保边框完整)
    if (option.state & QStyle::State_Selected) {
        painter->save();
        QPen pen(QColor("#094771"), 3);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        // 调整矩形以使 3px 边框居中对齐或内对齐，避免被切掉
        painter->drawRoundedRect(thumbRect.adjusted(1, 1, -1, -1), 6, 6);
        painter->restore();
    }

    // 3. 文件名标签（卡片下方）
    QString name = index.data(Qt::DisplayRole).toString();
    painter->setPen(QColor("#D4D4D4"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                      option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width()));

    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    const int textHeight = 30;
    // 编辑器精确定位到卡片下方的文字区域
    QRect textRect = option.rect.adjusted(4, option.rect.height() - textHeight, -4, -4);
    editor->setGeometry(textRect);
}

} // namespace ArcMeta
