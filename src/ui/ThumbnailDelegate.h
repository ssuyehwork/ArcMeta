#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include "UiHelper.h"

namespace ArcMeta {

class ThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ThumbnailDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        QRect rect = option.rect;

        // 绘制背景 (选中/悬停)
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(rect, option.palette.highlight());
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(rect, QColor(255, 255, 255, 20));
        }

        // 获取缩略图数据
        QVariant decoration = index.data(Qt::DecorationRole);
        QPixmap pixmap;
        bool isThumbnail = false;

        if (decoration.canConvert<QPixmap>()) {
            pixmap = decoration.value<QPixmap>();
            // 简单的启发式：如果 pixmap 的尺寸接近 iconSize，我们认为是缩略图
            // 在 ScanTableModel 中，缩略图是 QPixmap，而普通图标通常也是 QPixmap 但来自 getCachedIcon
            // 我们需要一种更明确的方式区分。
            // 可以在 model 中加一个自定义 role
            isThumbnail = index.data(Qt::UserRole + 1).toBool();
        }

        if (isThumbnail && !pixmap.isNull()) {
            // 缩略图自适应填满 rect (保持比例)
            QSize thumbSize = pixmap.size();
            thumbSize.scale(rect.size(), Qt::KeepAspectRatio);
            QRect drawRect(rect.center().x() - thumbSize.width() / 2,
                           rect.center().y() - thumbSize.height() / 2,
                           thumbSize.width(), thumbSize.height());
            painter->drawPixmap(drawRect, pixmap);
        } else {
            // 缩略图加载中或加载失败，居中显示文件类型图标
            QIcon icon;
            if (decoration.canConvert<QIcon>()) {
                icon = decoration.value<QIcon>();
            } else if (decoration.canConvert<QPixmap>()) {
                icon = QIcon(decoration.value<QPixmap>());
            }

            if (!icon.isNull()) {
                QSize iconSize(48, 48);
                // 确保图标不大于单元格
                if (iconSize.width() > rect.width() * 0.8) iconSize.setWidth(rect.width() * 0.8);
                if (iconSize.height() > rect.height() * 0.8) iconSize.setHeight(rect.height() * 0.8);

                QPoint center = rect.center();
                QRect iconRect(center.x() - iconSize.width() / 2,
                               center.y() - iconSize.height() / 2,
                               iconSize.width(), iconSize.height());
                icon.paint(painter, iconRect);
            }
        }

        // 绘制文字 (文件名) - 只在详情模式之外的模式可能需要
        // 但 ScanDialog 的 Justified 布局通常只显示图，或者图下方带字。
        // 这里根据需求，如果 Justified 布局只需要图，可以不画字。
        // 如果需要画字，可以在底部留出空间。

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QStyledItemDelegate::sizeHint(option, index);
    }
};

} // namespace ArcMeta
