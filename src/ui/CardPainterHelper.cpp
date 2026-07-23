#include "CardPainterHelper.h"
#include "UiHelper.h"
#include <QPainterPath>
#include <QFont>
#include <QtMath>

namespace ArcMeta {

void CardPainterHelper::drawCardCover(QPainter* painter, const QRect& cardRect, bool isSelected, 
                                     bool hasThumb, const QPixmap& thumb, const QIcon& defaultIcon, 
                                     bool isGridMode, bool isWaitingThumb) {
    Q_UNUSED(isSelected);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 绘制内容与裁剪 (Cover 模式)
    painter->save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(cardRect, 6, 6);
    painter->setClipPath(clipPath);

    // 绘制卡片背景
    painter->setPen(Qt::NoPen);
    painter->setBrush(isWaitingThumb ? QColor("#3A3A3A") : QColor("#2d2d2d"));
    painter->drawRect(cardRect);

    if (hasThumb) {
        if (!thumb.isNull()) {
            QPixmap scaled = thumb.scaled(cardRect.size(), 
                                          isGridMode ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding, 
                                          Qt::SmoothTransformation);
            int x = cardRect.center().x() - scaled.width() / 2;
            int y = cardRect.center().y() - scaled.height() / 2;
            painter->drawPixmap(x, y, scaled);
        }
    } else {
        if (!defaultIcon.isNull()) {
            // 针对普通文件（非图形/视频），保持 60% 比例缩小的图标绘制逻辑
            int iconSize = qMin(cardRect.width(), cardRect.height()) * 0.6;
            QRect iconRect(cardRect.center().x() - iconSize / 2,
                           cardRect.center().y() - iconSize / 2,
                           iconSize, iconSize);
            defaultIcon.paint(painter, iconRect);
        }
    }
    painter->restore();
    painter->restore();
}

void CardPainterHelper::drawCardBorder(QPainter* painter, const QRect& cardRect, bool isSelected) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (isSelected) {
        painter->setPen(QPen(QColor("#3498db"), 3));
    } else {
        painter->setPen(QPen(QColor("#4a4a4a"), 1));
    }
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(cardRect, 6, 6);
    painter->restore();
}

void CardPainterHelper::drawStatusIndicators(QPainter* painter, const QRect& cardRect, 
                                             bool isPinned, bool isManaged, bool isDir, double progress) {
    QRect statusRect(cardRect.right() - 22, cardRect.top() + 8, 16, 16);
    if (isPinned) {
        UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, statusRect);
    } else if (isDir && progress >= 0.0 && progress < 1.0) {
        // --- 绘制进度环 (开箱即用代码) --- 
        painter->save(); 
        painter->setRenderHint(QPainter::Antialiasing); 
         
        // 1. 底环 
        painter->setPen(QPen(QColor(60, 60, 60, 180), 2)); 
        painter->drawEllipse(statusRect.adjusted(1, 1, -1, -1)); 
         
        // 2. 进度弧 (品牌蓝 #3498db) 
        QPen pPen(QColor("#3498db"), 2); 
        pPen.setCapStyle(Qt::RoundCap); 
        painter->setPen(pPen); 
         
        int spanAngle = -qRound(progress * 360 * 16); // 逆时针计算 
        painter->drawArc(statusRect.adjusted(1, 1, -1, -1), 90 * 16, spanAngle); 
        painter->restore(); 
    } else if (isManaged || (isDir && progress >= 1.0)) {
        UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, statusRect);
    }
}

void CardPainterHelper::drawExtensionBadge(QPainter* painter, const QRect& cardRect, 
                                           const QString& ext, bool hasThumb) {
    QColor badgeColor = UiHelper::getExtensionColor(ext);

    // 物理优化：针对无缩略图项应用半透明角标，减少视觉冲击
    if (!hasThumb) {
        badgeColor.setAlpha(160);
    }

    QRect extRect(cardRect.left() + 8, cardRect.top() + 8, 36, 18);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeColor);
    painter->drawRoundedRect(extRect, 2, 2);
    painter->setPen(hasThumb ? QColor("#FFFFFF") : QColor(255, 255, 255, 180));
    QFont extFont = painter->font(); extFont.setPointSize(8); extFont.setBold(true);
    painter->setFont(extFont);
    painter->drawText(extRect, Qt::AlignCenter, ext);
    painter->restore();
}

void CardPainterHelper::drawRatingStars(QPainter* painter, const QRect& banRect, 
                                        const QRect& cardRect, int starSize, int starSpacing, int ratingY, int ratingH, int starsStartX,
                                        int rating, const QString& colorStr, bool isSelected) {
    Q_UNUSED(cardRect);
    // 逻辑重构：彩色胶囊背景独立于星级显示
    if (!colorStr.isEmpty()) {
        QColor bgColor = UiHelper::parseColorName(colorStr);
        if (bgColor.isValid()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(bgColor);
            painter->setPen(Qt::NoPen);
            
            QRect lastStarRect(starsStartX + 4 * (starSize + starSpacing), 
                               ratingY + (ratingH - starSize) / 2, 
                               starSize, starSize);
            QRect totalRect = banRect.united(lastStarRect);
            painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
            painter->restore();
        }
    }

    bool shouldShowRating = (rating > 0) || isSelected || !colorStr.isEmpty();
    if (shouldShowRating) {
        bool drawStars = (rating > 0) || (isSelected && colorStr.isEmpty());

        if (drawStars) {
            QColor bgColor = colorStr.isEmpty() ? QColor(0,0,0,0) : UiHelper::parseColorName(colorStr);

            // 物理修复：采用感知亮度对比色计算
            double luminance = 0.0;
            if (bgColor.isValid() && bgColor.alpha() > 0) {
                luminance = (0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue()) / 255.0;
            }

            QColor starColor, emptyStarColor;
            if (colorStr.isEmpty()) {
                starColor      = QColor("#CCCCCC");
                emptyStarColor = QColor("#888888");
            } else if (luminance < 0.5) {
                starColor      = QColor("#FFFFFF");
                emptyStarColor = QColor(255, 255, 255, 160);
            } else {
                starColor      = QColor("#1A1A1A");
                emptyStarColor = QColor(0, 0, 0, 140);
            }

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            UiHelper::getIcon("no_color", starColor, banRect.width()).paint(painter, banRect);
            QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), starColor);
            QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), emptyStarColor);
            for (int i = 0; i < 5; ++i) {
                QRect starRect(starsStartX + i * (starSize + starSpacing),
                               ratingY + (ratingH - starSize) / 2,
                               starSize, starSize);
                painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
            }
            painter->restore();
        }
    }
}

void CardPainterHelper::drawEmptyFolderBorder(QPainter* painter, const QRect& cardRect) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor("#41F2F2"), 1, Qt::DashLine));
    painter->setBrush(Qt::NoBrush); // 确保空文件夹标记为全透明
    painter->drawRoundedRect(cardRect, 6, 6);
    painter->restore();
}

void CardPainterHelper::drawCategoryBackground(QPainter* painter, const QRect& contentRect, bool isSelected, bool isHover, const QString& colorHex) {
    if (!isSelected && !isHover) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QColor baseColor = colorHex.isEmpty() ? QColor("#3498db") : QColor(colorHex);
    QColor bg = isSelected ? baseColor : QColor("#2a2d2e");
    if (isSelected) {
        bg.setAlphaF(0.2f); 
    }

    painter->setBrush(bg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(contentRect, 4, 4);
    painter->restore();
}

} // namespace ArcMeta
