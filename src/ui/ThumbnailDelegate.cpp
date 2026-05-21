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

    // ① 强制绘制底层背景（解决选中色污染）
    painter->fillRect(option.rect, QColor("#1E1E1E"));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ② 圆角裁剪（仅作用于卡片）
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

    // ③ 选中态不再使用蒙版，以保持图像原色 (根据用户反馈移除 fillRect)

    painter->restore(); // ← 释放裁剪区（与上方 save 对应）

    // ④ 选中边框（在裁剪区外绘制，确保完整显示）
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (option.state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor("#3498db"), 2)); // 改回经典蓝色边框
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 6, 6);
    }

    // ⑤ 文件名颜色同步
    painter->setPen(option.state & QStyle::State_Selected
                    ? QColor("#3498db") : QColor("#C8C8C8"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
        option.fontMetrics.elidedText(
            index.data(Qt::DisplayRole).toString(),
            Qt::ElideMiddle, textRect.width()));

    painter->restore(); // ← 与第二个 save 对应
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (editor) {
        editor->setStyleSheet(
            "background-color: #2D2D2D; color: white; selection-background-color: #3498db; "
            "border: 1px solid #3498db; border-radius: 4px; padding: 0 4px;"
        );
    }
    return editor;
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    const int textHeight = 36;
    QRect textRect(option.rect.left() + 4,
                   option.rect.bottom() - textHeight,
                   option.rect.width() - 8,
                   textHeight - 4);
    editor->setGeometry(textRect);
}

} // namespace ArcMeta
