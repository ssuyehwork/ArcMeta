#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include "CategoryModel.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;

namespace ArcMeta {

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setSearchKeyword(const QString& keyword) {
        m_searchKeyword = keyword;
    }
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.isValid()) return;

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

            QString colorHex = index.data(ColorRole).toString();
            QColor baseColor = colorHex.isEmpty() ? QColor("#3498db") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
            if (selected) bg.setAlphaF(0.2f); 

            // 2026-03-xx 按照用户要求：物理隔离 branch 区域，解决选中背景遮挡折叠图标的问题
            // 严格执行宪法第五定律第 6 条：padding: 2px 4px, margin: 1px 2px
            QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            QRect decoRect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, option.widget);
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
            
            // 高亮矩形仅包含图标与文本，不触碰左侧 branch 区域
            QRect contentRect = decoRect.united(textRect);
            
            // 2026-03-xx 物理对齐修正：确保 contentRect 的左边界从图标开始，右边界延展至 widget 边缘（减去右边距）
            if (option.widget) {
                contentRect.setRight(option.widget->width() - 4);
            }

            // 应用宪法规范：margin 1px 2px (上下 1px, 左右 2px)
            contentRect.adjust(2, 1, -2, -1);
            
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 4, 4);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        
        // 2026-07-18 按照 Plan-96：支持搜索高亮渲染
        if (m_searchKeyword.isEmpty()) {
            QStyledItemDelegate::paint(painter, opt, index);
        } else {
            // 1. 先让基类画图标 (不画文字)
            QStyleOptionViewItem optNoText = opt;
            optNoText.text = "";
            QStyledItemDelegate::paint(painter, optNoText, index);

            // 2. 获取文字区域并自行绘制高亮
            // Plan-97: 仅高亮 Name 部分，不包含 (n) 计数器
            QString fullText = index.data(Qt::DisplayRole).toString();
            QString nameText = index.data(NameRole).toString();
            QString counterText = fullText.mid(nameText.length());

            QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
            textRect.adjust(0, 0, -5, 0); // 右侧呼吸

            painter->save();
            painter->setRenderHint(QPainter::TextAntialiasing);

            // 仅在 Name 部分计算匹配位置
            int index_match = nameText.indexOf(m_searchKeyword, 0, Qt::CaseInsensitive);
            if (index_match >= 0) {
                QFont font = opt.font;
                painter->setFont(font);

                QFontMetrics fm(font);
                QString preText = nameText.left(index_match);
                QString midText = nameText.mid(index_match, m_searchKeyword.length());
                QString postText = nameText.mid(index_match + m_searchKeyword.length()) + counterText;

                // 绘制逻辑
                int x = textRect.left();
                int y = textRect.top() + (textRect.height() + fm.ascent() - fm.descent()) / 2;

                // 绘制前段
                painter->setPen(opt.palette.color(QPalette::Text));
                painter->drawText(x, y, preText);
                x += fm.horizontalAdvance(preText);

                // 绘制匹配段 (高亮)
                painter->setPen(QColor("#41F2F2")); // 按照 Plan-97 规范使用亮蓝色
                painter->drawText(x, y, midText);
                x += fm.horizontalAdvance(midText);

                // 绘制后段 (含计数器)
                painter->setPen(opt.palette.color(QPalette::Text));
                painter->drawText(x, y, postText);
            } else {
                // 虽然有搜索词但此项可能因为是父项被显示而本身没匹配
                QStyledItemDelegate::paint(painter, opt, index);
            }
            painter->restore();
        }
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        QLineEdit* editor = new QLineEdit(parent);
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 6px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "}"
        );
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
        textRect.adjust(0, -1, 0, 1);
        editor->setGeometry(textRect);
    }

private:
    QString m_searchKeyword;
};

} // namespace ArcMeta
