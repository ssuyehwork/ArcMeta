#ifndef CATEGORYDELEGATE_H
#define CATEGORYDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include "../models/CategoryModel.h"

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

        // [CRITICAL] 编辑状态下跳过自定义绘制，避免背景颜色与编辑器冲突
        if (option.state & QStyle::State_Editing) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        bool selected = option.state & QStyle::State_Selected;
        bool hover = option.state & QStyle::State_MouseOver;
        bool isSelectable = index.flags() & Qt::ItemIsSelectable;

        if (isSelectable && (selected || hover)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            QString colorHex = index.data(CategoryModel::ColorRole).toString();
            QColor baseColor = colorHex.isEmpty() ? QColor("#4a90e2") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.2); // 选中时应用 20% 透明度联动分类颜色

            // 精准计算高亮区域：联合图标与文字区域，避开左侧缩进/箭头区域
            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect decoRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
            
            // 联合区域并与当前行 rect 取交集，防止溢出
            QRect contentRect = decoRect.united(textRect);
            contentRect = contentRect.intersected(option.rect);
            
            // 向左右微调 (padding)，并保持上下略有间隙以体现圆角效果
            // [FIX] 左侧调整由 -6 改为 0，防止覆盖树状结构的展开箭头
            contentRect.adjust(0, 1, 0, -1);
            
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 5, 5);
            painter->restore();
        }

        // 绘制原内容 (图标、文字)
        QStyleOptionViewItem opt = option;
        // 关键：移除 Selected 状态，由我们自己控制背景，防止 QStyle 绘制默认的蓝色/灰色整行高亮
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        // 选中时文字强制设为白色以确保清晰度
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QLineEdit* editor = new QLineEdit(parent);
        // [CRITICAL] 优化编辑器样式，增加 padding 解决“挤压”感，并统一配色
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 4px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "}"
        );
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        // [CRITICAL] 精准定位编辑器区域：仅覆盖文本部分，不遮挡左侧图标与箭头空间，解决“挤压”感
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
        
        // 稍微上下扩展，使得输入框在 22px 行高中不显得过于局促
        textRect.adjust(0, -1, 0, 1);
        editor->setGeometry(textRect);
    }
};

#endif // CATEGORYDELEGATE_H
