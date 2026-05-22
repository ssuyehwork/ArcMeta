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

    // ① 悬停状态 (Hover) 背景
    if (option.state & QStyle::State_MouseOver) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 15)); // 轻微提亮
        painter->drawRoundedRect(cardRect, 6, 6);
    }

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

    // [V3 物理优化] 彻底移除选中高亮叠加层，确保图片颜色准确性
    // 删除了原有的 painter->fillRect(cardRect, QColor(255, 140, 0, 50)) 逻辑

    // ③ 信息密度增强：在缩略图上以半透明层形式叠加载入评分
    int rating = index.data(Qt::UserRole + 3).toInt(); // 2026-06-xx 性能优化：直接从 Model 获取评分
    if (rating > 0) {
        // 物理修复：不重置 ClipPath，而是利用现有 ClipPath 确保评分条也有圆角
        // 绘制底部半透明遮罩层
        int barHeight = 24;
        QRect ratingBar(cardRect.left(), cardRect.bottom() - barHeight, cardRect.width(), barHeight);
        painter->fillRect(ratingBar, QColor(0, 0, 0, 100));

        // 绘制评分星级
        int starSize = 14;
        int starSpacing = 2;
        int totalW = 5 * starSize + 4 * starSpacing;
        int startX = ratingBar.left() + (ratingBar.width() - totalW) / 2;
        int startY = ratingBar.top() + (ratingBar.height() - starSize) / 2;

        // 2026-06-xx 性能优化：静态缓存 Pixmap 避免重复创建
        static QPixmap filledStar, emptyStar;
        static int lastStarSize = -1;
        if (lastStarSize != starSize) {
            filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#EF9F27"));
            emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), QColor("#888888"));
            lastStarSize = starSize;
        }

        for (int i = 0; i < 5; ++i) {
            QRect starRect(startX + i * (starSize + starSpacing), startY, starSize, starSize);
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
        }
    }

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
