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
    const int textHeight = 52; // 2026-06-xx 物理同步：增加高度以容纳外部化的评分区
    const int ratingHeight = 22; // 2026-06-xx 物理对标：评分区高度

    QRect cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + 3));

    // 2026-06-xx 物理对标：评分区位于卡片正下方
    QRect ratingRect = QRect(option.rect.left() + 3,
                             cardRect.bottom() + 4,
                             option.rect.width() - 6,
                             ratingHeight);

    // 2026-06-xx 物理对标：名称区位于评分区下方
    QRect textRect = QRect(option.rect.left() + 3,
                           ratingRect.bottom() + 2,
                           option.rect.width() - 6,
                           textHeight - ratingHeight - 8);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 背景与边框绘制 (对标 ContentPanel 风格)
    bool isSelected = (option.state & QStyle::State_Selected);
    bool isHovered = (option.state & QStyle::State_MouseOver);
    QColor cardBg = isSelected ? QColor("#282828") : (isHovered ? QColor("#2A2A2A") : QColor("#2D2D2D"));
    painter->setPen(isSelected ? QPen(QColor("#3498db"), 2) : QPen(QColor("#333333"), 1));
    painter->setBrush(cardBg);
    painter->drawRoundedRect(cardRect, 8, 8);

    // ② 圆角裁剪（仅作用于卡片内容绘制）
    QPainterPath clipPath;
    clipPath.addRoundedRect(cardRect.adjusted(1, 1, -1, -1), 8, 8);
    painter->setClipPath(clipPath);

    // [V3 物理补完] 1. 置顶/已录入状态图标绘制 (对标 ContentPanel)
    bool isPinned = index.data(Qt::UserRole + 4).toBool();
    bool isManaged = index.data(Qt::UserRole + 5).toBool();
    if (isPinned || isManaged) {
        QRect statusRect(cardRect.right() - 22, cardRect.top() + 8, 16, 16);
        if (isPinned) {
            QIcon pinIcon = UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16);
            pinIcon.paint(painter, statusRect);
        } else {
            QIcon checkIcon = UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16);
            checkIcon.paint(painter, statusRect);
        }
    }

    // [V3 物理补完] 2. 扩展名角标绘制
    QString ext = index.data(Qt::UserRole + 6).toString();
    QColor badgeColor = UiHelper::getExtensionColor(ext);
    QRect extRect(cardRect.left() + 8, cardRect.top() + 8, 36, 18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeColor);
    painter->drawRoundedRect(extRect, 2, 2);
    painter->setPen(QColor("#FFFFFF"));
    QFont extFont = painter->font();
    extFont.setPointSize(8);
    extFont.setBold(true);
    painter->setFont(extFont);
    painter->drawText(extRect, Qt::AlignCenter, ext);

    bool hasThumb = index.data(Qt::UserRole + 1).toBool();
    QPixmap thumb = index.data(Qt::DecorationRole).value<QPixmap>();

    if (hasThumb && !thumb.isNull()) {
        // 2026-06-xx 物理同步：适配 High DPI 填充裁剪模式
        qreal dpr = painter->device()->devicePixelRatio();
        QSize pixelSize = cardRect.size() * dpr;
        QPixmap scaled = thumb.scaled(pixelSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);

        int x = cardRect.center().x() - qRound(scaled.width() / dpr) / 2;
        int y = cardRect.center().y() - qRound(scaled.height() / dpr) / 2;
        painter->drawPixmap(x, y, scaled);
    } else {
        painter->fillRect(cardRect, QColor("#2D2D2D"));
        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QPoint center = cardRect.center();
        if (!icon.isNull())
            icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }

    painter->restore(); // 释放裁剪区

    // ③ 评分区域绘制 (对标 ContentPanel 逻辑)
    int rating = index.data(Qt::UserRole + 3).toInt();
    bool shouldShowRating = (rating > 0) || isSelected;

    if (shouldShowRating) {
        int starSize = 14;
        int starSpacing = 2;
        int banW = 12;
        int banGap = 4;
        int infoTotalW = banW + banGap + (5 * starSize) + (4 * starSpacing);
        int infoStartX = ratingRect.left() + (ratingRect.width() - infoTotalW) / 2;

        QRect banRect(infoStartX, ratingRect.top() + (ratingRect.height() - banW) / 2, banW, banW);
        int starsStartX = infoStartX + banW + banGap;

        // 绘制禁止图标 (如果未打分但选中，则显示)
        QIcon banIcon = UiHelper::getIcon("no_color", QColor("#B0B0B0"), banRect.width());
        banIcon.paint(painter, banRect);

        // 静态缓存星级 Pixmap (含 High DPI 适配)
        static QPixmap filledStar, emptyStar;
        static int lastStarSize = -1;
        static qreal lastDpr = -1.0;
        qreal currentDpr = painter->device()->devicePixelRatio();

        if (lastStarSize != starSize || lastDpr != currentDpr) {
            QSize pixelSize = QSize(starSize, starSize) * currentDpr;
            filledStar = UiHelper::getPixmap("star_filled", pixelSize, QColor("#EF9F27"));
            emptyStar = UiHelper::getPixmap("star", pixelSize, QColor("#888888"));
            filledStar.setDevicePixelRatio(currentDpr);
            emptyStar.setDevicePixelRatio(currentDpr);
            lastStarSize = starSize;
            lastDpr = currentDpr;
        }

        for (int i = 0; i < 5; ++i) {
            QRect starRect(starsStartX + i * (starSize + starSpacing), ratingRect.top() + (ratingRect.height() - starSize) / 2, starSize, starSize);
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
        }
    }

    // ③ 文件名（评分区下方）
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
    const int textHeight = 52;
    const int ratingHeight = 22;
    // 2026-06-xx 物理修正：编辑器仅覆盖名称区域
    QRect textRect(option.rect.left() + 4,
                   option.rect.bottom() - (textHeight - ratingHeight - 4),
                   option.rect.width() - 8,
                   (textHeight - ratingHeight - 8));
    editor->setGeometry(textRect);
}

} // namespace ArcMeta
