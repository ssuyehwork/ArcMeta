#pragma once

#include <QPainter>
#include <QRect>
#include <QPixmap>
#include <QIcon>
#include <QString>
#include <QColor>

namespace ArcMeta {

class CardPainterHelper {
public:
    // 1. 绘制主体卡片底色及缩略图 Cover
    static void drawCardCover(QPainter* painter, const QRect& cardRect, bool isSelected, 
                             bool hasThumb, const QPixmap& thumb, const QIcon& defaultIcon, 
                             bool isGridMode, bool isWaitingThumb);

    // 2. 绘制卡片圆角边框 (选中 3px 蓝色，未选中 1px #4a4a4a)
    static void drawCardBorder(QPainter* painter, const QRect& cardRect, bool isSelected);

    // 3. 绘制状态互斥标记及进度环
    static void drawStatusIndicators(QPainter* painter, const QRect& cardRect, 
                                     bool isPinned, bool isManaged, bool isDir, double progress);

    // 4. 绘制自适应扩展名徽章
    static void drawExtensionBadge(QPainter* painter, const QRect& cardRect, 
                                   const QString& ext, bool hasThumb);

    // 5. 绘制评级星级与彩色胶囊底色
    static void drawRatingStars(QPainter* painter, const QRect& banRect, 
                                const QRect& cardRect, int starSize, int starSpacing, int ratingY, int ratingH, int starsStartX,
                                int rating, const QString& colorStr, bool isSelected);

    // 6. 绘制空文件夹特异虚线边框
    static void drawEmptyFolderBorder(QPainter* painter, const QRect& cardRect);

    // 7. 绘制分类侧边栏节点的高亮/彩色背景底色
    static void drawCategoryBackground(QPainter* painter, const QRect& contentRect, bool isSelected, bool isHover, const QString& colorHex);
};

} // namespace ArcMeta
