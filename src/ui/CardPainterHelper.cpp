#include "CardPainterHelper.h"
#include "UiHelper.h"
#include <QPainterPath>
#include <QFont>
#include <QtMath>

namespace QuarkMeta {

void CardPainterHelper::drawCardCover(QPainter* painter, const QRect& cardRect, bool isSelected, 
                                     bool hasThumb, const QPixmap& thumb, const QIcon& defaultIcon, 
                                     bool isGridMode, bool isWaitingThumb) {
    Q_UNUSED(isSelected);
    Q_UNUSED(isGridMode);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 设置卡片圆角裁切
    QPainterPath clipPath;
    clipPath.addRoundedRect(cardRect, 6, 6);
    painter->setClipPath(clipPath);

    // ② 🚨 严格保持透明：卡片内部 100% 完全透明（绝不绘制任何不透明底色）
    painter->setPen(Qt::NoPen);
    if (isWaitingThumb) {
        painter->setBrush(QColor("#2A2A2A")); // 仅在等待缩略图时显示轻量占位灰底
        painter->drawRect(cardRect);
    } else {
        painter->setBrush(Qt::transparent);  // ✅ 常规状态保持完全透明！
        painter->drawRect(cardRect);
    }

    if (hasThumb && !thumb.isNull()) {
        // 硬件加速原生绘制：按比例计算目标区域，零 CPU 图像重采样与内存分配
        QSize imgSize = thumb.size();
        QSize targetSize = imgSize.scaled(cardRect.size(), Qt::KeepAspectRatio);
        QRect targetRect(cardRect.center().x() - targetSize.width() / 2,
                         cardRect.center().y() - targetSize.height() / 2,
                         targetSize.width(), targetSize.height());
        painter->drawPixmap(targetRect, thumb);
    } else if (!defaultIcon.isNull()) {
        // 非图片文件图标：65% 比例居中悬浮展示
        int iconSize = qMin(cardRect.width(), cardRect.height()) * 0.65;

        // 关键修复：不要直接向 QIcon 请求任意大尺寸的 pixmap ——
        // 部分来源（如基于虚构 dummy 文件名提取的 Shell 关联图标）在被请求超出其
        // 原生可用尺寸时，会触发 Windows Shell 对"大图标/Jumbo 图标"的重新提取，
        // 若源文件不存在会导致提取失败，退化为空白通用图标。
        // 因此改为：先取该 QIcon 实际拥有最大原生尺寸位图，再手动平滑放大。
        QList<QSize> availSizes = defaultIcon.availableSizes();
        QSize nativeSize = availSizes.isEmpty() ? QSize(32, 32) : availSizes.last();
        QPixmap iconPixmap = defaultIcon.pixmap(nativeSize);

        if (iconPixmap.isNull()) {
            // 兜底：万一原生尺寸也取不到，再退回按目标尺寸请求一次
            iconPixmap = defaultIcon.pixmap(iconSize, iconSize);
        }

        if (!iconPixmap.isNull() && (iconPixmap.width() != iconSize || iconPixmap.height() != iconSize)) {
            iconPixmap = iconPixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        QRect iconRect(cardRect.center().x() - iconPixmap.width() / 2,
                       cardRect.center().y() - iconPixmap.height() / 2,
                       iconPixmap.width(), iconPixmap.height());

        painter->drawPixmap(iconRect, iconPixmap);
    }
    painter->restore();
}

void CardPainterHelper::drawCardBorder(QPainter* painter, const QRect& cardRect, bool isSelected) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (isSelected) {
        // 1. 选中状态：2 像素品牌蓝高亮边框 (#3498db)，贴合卡片边缘
        painter->setPen(QPen(QColor("#3498db"), 2));
    } else {
        // 2. 🚨 恢复要求：未选中状态恢复 2 像素深灰套边 (#4a4a4a)
        painter->setPen(QPen(QColor("#4a4a4a"), 2));
    }

    painter->setBrush(Qt::NoBrush);
    
    // 直接在 cardRect 原位绘制 6px 圆角套边，无多余间隙
    painter->drawRoundedRect(cardRect, 6, 6);
    
    painter->restore();
}

void CardPainterHelper::drawStatusIndicators(QPainter* painter, const QRect& cardRect, 
                                             bool isPinned, bool isManaged, bool isDir, double progress) {
    QRect statusRect(cardRect.right() - 22, cardRect.top() + 8, 16, 16);
    if (isPinned) {
        UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, statusRect);
    } else if (isDir && progress >= 0.0 && progress < 1.0) {
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
    Q_UNUSED(starSpacing);

    int unifiedSpacing = -4;

    if (!colorStr.isEmpty()) {
        QColor bgColor = UiHelper::parseColorName(colorStr);
        if (bgColor.isValid()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(bgColor);
            painter->setPen(Qt::NoPen);
            
            QRect lastStarRect(starsStartX + 4 * (starSize + unifiedSpacing), 
                               ratingY + (ratingH - starSize) / 2, 
                               starSize, starSize);
            QRect totalRect = banRect.united(lastStarRect);
            painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
            painter->restore();
        }
    }

    bool drawStars = (rating > 0) || isSelected;
    if (drawStars) {
        QColor bgColor = colorStr.isEmpty() ? QColor(0,0,0,0) : UiHelper::parseColorName(colorStr);
        
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
        QPixmap emptyStar  = UiHelper::getPixmap("star", QSize(starSize, starSize), emptyStarColor);
        
        for (int i = 0; i < 5; ++i) {
            QRect starRect(starsStartX + i * (starSize + unifiedSpacing), 
                           ratingY + (ratingH - starSize) / 2, 
                           starSize, starSize);
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
        }
        painter->restore();
    }
}

void CardPainterHelper::drawEmptyFolderBorder(QPainter* painter, const QRect& cardRect) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor("#41F2F2"), 1, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
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

} // namespace QuarkMeta