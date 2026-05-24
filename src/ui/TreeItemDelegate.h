#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include "ContentPanel.h"
#include "UiHelper.h"

namespace ArcMeta {

/**
 * @brief 通用树形视图代理，提供圆角高亮效果
 */
class TreeItemDelegate : public QStyledItemDelegate {
public:
    explicit TreeItemDelegate(QObject* parent = nullptr, bool showStatus = true)
        : QStyledItemDelegate(parent), m_showStatus(showStatus) {}
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;

        if (selected || hover) {
            painter->save();
            // 2026-06-xx 按照用户最新要求：消除“坑坑洼洼”感，改用全行贯穿式直角高亮，填满整个区域
            QColor bg = selected ? QColor("#378ADD") : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.15f); 

            // 物理修复：直接使用 option.rect，不进行 adjust 缩进，不使用圆角，确保色块无缝对接
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRect(option.rect);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
        } else if (m_showStatus) {
            // 2026-06-xx 按照视觉要求：未录入项文字半透明暗淡处理
            // 物理修复：校准作用域
            bool isManaged = index.data(InDatabaseRole).toBool();
            if (!isManaged) {
                opt.palette.setColor(QPalette::Text, QColor(238, 238, 238, 120));
            }
        }

        // 2026-06-08 按照 7 列架构重构：第 1, 2, 3 列由代理独立绘制，其他列由基类处理
        int col = index.column();
        if (col == 1 || col == 2 || col == 3) {
            // 这三列不调用默认 paint，完全自定义
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            if (col == 1) { // 状态列
                bool isPinned = index.model()->index(index.row(), 0).data(IsLockedRole).toBool();
                bool isManaged = index.model()->index(index.row(), 0).data(InDatabaseRole).toBool();
                if (isPinned || isManaged) {
                    QRect iconRect(option.rect.left() + (option.rect.width() - 16) / 2,
                                   option.rect.top() + (option.rect.height() - 16) / 2, 16, 16);
                    if (isPinned) {
                        UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, iconRect);
                    } else {
                        UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, iconRect);
                    }
                }
            } else if (col == 2) { // 星级列
                int rating = index.model()->index(index.row(), 0).data(RatingRole).toInt();
                if (rating > 0) {
                    int starSize = 14;
                    int spacing = 1;
                    int totalW = 5 * starSize + 4 * spacing;
                    int startX = option.rect.left() + (option.rect.width() - totalW) / 2;
                    QPixmap star = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(starSize, starSize), QColor("#FECF0E"));
                    for (int i = 0; i < rating; ++i) {
                        painter->drawPixmap(startX + i * (starSize + spacing), 
                                            option.rect.top() + (option.rect.height() - starSize) / 2, star);
                    }
                }
            } else if (col == 3) { // 颜色列
                QString colorHex = index.model()->index(index.row(), 0).data(ColorRole).toString();
                if (!colorHex.isEmpty()) {
                    QColor c = UiHelper::parseColorName(colorHex);
                    if (c.isValid()) {
                        painter->setBrush(c);
                        painter->setPen(Qt::NoPen);
                        painter->drawEllipse(option.rect.center(), 6, 6);
                    }
                }
            }
            painter->restore();
        } else {
            QStyledItemDelegate::paint(painter, opt, index);
        }
    }

private:
    bool m_showStatus;
};

} // namespace ArcMeta
