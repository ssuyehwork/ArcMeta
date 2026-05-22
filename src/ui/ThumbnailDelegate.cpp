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
        // 2026-06-xx 物理修复：配合 JustifiedView 的精确计算，将缩放模式改为 KeepAspectRatio。
        // 这确保了在任何舍入误差下图片都不会被截断。
        QPixmap scaled = thumb.scaled(cardRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

    // [V3 物理优化] 彻底移除选中高亮叠加层，确保图片颜色准确性
    // 删除了原有的 painter->fillRect(cardRect, QColor(255, 140, 0, 50)) 逻辑

    painter->restore(); // 释放裁剪区

    // ② 选中边框（在裁剪区外绘制，确保完整显示）
    if (option.state & QStyle::State_Selected) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // 按照用户要求：修改为项目标准蓝 (#3498db)
        painter->setPen(QPen(QColor("#3498db"), 2)); 
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(cardRect.adjusted(0, 0, 0, 0), 6, 6);
        painter->restore();
    }

    // ③ 文件名（卡片下方）
    painter->save();
    painter->setPen(option.state & QStyle::State_Selected
                    ? QColor("#3498db") : QColor("#C8C8C8"));
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
        option.fontMetrics.elidedText(
            index.data(Qt::DisplayRole).toString(),
            Qt::ElideMiddle, textRect.width()));
    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (editor) {
        // 按照用户要求：修改为项目标准蓝 (#3498db)
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
    // 按照用户要求：修正编辑器位置。
    // 计算文字区域：位于整体区域底部 textHeight 像素
    QRect textRect(option.rect.left() + 4,
                   option.rect.bottom() - textHeight,
                   option.rect.width() - 8,
                   textHeight - 4);
    editor->setGeometry(textRect);
}

} // namespace ArcMeta
