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
            painter->setRenderHint(QPainter::Antialiasing);

            // 2026-06-xx 按照要求：美化高亮效果，消除“坑坑洼洼”感，采用全行贯穿式圆角高亮
            QColor bg = selected ? QColor("#378ADD") : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.15f);

            // 使用 option.rect 确保高亮条横向覆盖整个可视区域
            QRect highlightRect = option.rect;
            highlightRect.adjust(4, 1, -4, -1); // 左右留出少量呼吸间距，上下微调对齐
            
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(highlightRect, 4, 4);
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

        QStyledItemDelegate::paint(painter, opt, index);

        if (m_showStatus) {
            // 2026-06-xx 按照要求：实现状态位图标互斥显示逻辑
            // 位置复用原则：在项的末尾（或原本置顶图标的位置）进行绘制
            // 物理修复：校准作用域
            bool isPinned = index.data(IsLockedRole).toBool();
            bool isManaged = index.data(InDatabaseRole).toBool();

            if (isPinned || isManaged) {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                
                // 计算状态位图标的矩形区域 (位于项的最右侧，预留 20px 宽度)
                QRect statusRect = option.rect;
                statusRect.setLeft(statusRect.right() - 24);
                statusRect.setWidth(16);
                statusRect.setTop(statusRect.top() + (statusRect.height() - 16) / 2);
                statusRect.setHeight(16);

                if (isPinned) {
                    // 1. 置顶优先：显示置顶图标
                    QIcon pinIcon = UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16);
                    pinIcon.paint(painter, statusRect);
                } else {
                    // 2. 已录入但未置顶：在该位置显示绿对勾图标
                    QIcon checkIcon = UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16);
                    checkIcon.paint(painter, statusRect);
                }
                painter->restore();
            }
        }
    }

private:
    bool m_showStatus;
};

} // namespace ArcMeta
