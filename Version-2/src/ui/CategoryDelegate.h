#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include <QAbstractProxyModel>
#include "CategoryModel.h"
#include "CategoryFilterProxyModel.h"
#include "CardPainterHelper.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;

namespace ArcMeta {

class CategoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    
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
            // 当未指定设色时，默认回退色采用标志性的深灰色 (#555555)（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”）
            QColor baseColor = colorHex.isEmpty() ? QColor("#555555") : QColor(colorHex);
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
            
            ArcMeta::CardPainterHelper::drawCategoryBackground(painter, contentRect, selected, hover, colorHex);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
            opt.palette.setColor(QPalette::HighlightedText, Qt::white);
        }
        
        // 2026-xx-xx 按照 Plan-98：实现关键词高亮渲染
        QString filterText;
        const QAbstractProxyModel* proxy = qobject_cast<const QAbstractProxyModel*>(index.model());
        if (proxy) {
            const CategoryFilterProxyModel* filterProxy = qobject_cast<const CategoryFilterProxyModel*>(proxy);
            if (filterProxy) filterText = filterProxy->filterText();
        }

        if (!filterText.isEmpty()) {
            QString fullText = index.data(Qt::DisplayRole).toString();
            int start = fullText.indexOf(filterText, 0, Qt::CaseInsensitive);
            if (start >= 0) {
                // 执行自定义绘制
                painter->save();
                QStyle* style = option.widget ? option.widget->style() : QApplication::style();
                
                // 绘制图标 (Decoration)
                style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, option.widget);
                
                QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, option.widget);
                textRect.adjust(2, 0, 0, 0); // 微调
                
                // 计算文本布局
                QFont font = opt.font;
                painter->setFont(font);
                
                QString pre = fullText.left(start);
                QString mid = fullText.mid(start, filterText.length());
                QString post = fullText.mid(start + filterText.length());
                
                int x = textRect.left();
                int y = textRect.center().y() + painter->fontMetrics().ascent() / 2 - 1;

                // 绘制前缀
                painter->setPen(selected ? Qt::white : QColor("#CCCCCC"));
                painter->drawText(x, y, pre);
                x += painter->fontMetrics().horizontalAdvance(pre);
                
                // 绘制高亮中缀 (PrimaryBlue)
                painter->setPen(QColor("#3498db"));
                painter->drawText(x, y, mid);
                x += painter->fontMetrics().horizontalAdvance(mid);
                
                // 绘制后缀
                painter->setPen(selected ? Qt::white : QColor("#CCCCCC"));
                painter->drawText(x, y, post);
                
                painter->restore();
                return;
            }
        }

        QStyledItemDelegate::paint(painter, opt, index);
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
        // 2026-07-26 极致重构：为编辑器安装事件过滤器，确保 eventFilter 能有效捕获键盘冲突并拦截（对应用户原话：“在编辑状态下按下向上/向下方向键时则不该向上游动选中项目”）
        editor->installEventFilter(const_cast<CategoryDelegate*>(this));
        return editor;
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = reinterpret_cast<QKeyEvent*>(event); 
            QLineEdit* editor = qobject_cast<QLineEdit*>(obj); 
            if (editor) { 
                int key = keyEvent->key();
                if (key == Qt::Key_Up || key == Qt::Key_Down) {
                    keyEvent->accept();
                    return true; // 彻底吞噬，不让 View 漂移（对应用户原话：“用户按下向上/向下方向键时则不该向上游动选中上方/下方的项目”）
                }
                if (key == Qt::Key_Left || key == Qt::Key_Right) {
                    if (editor->hasSelectedText()) {
                        // 全选高亮状态（对应用户原话：“如果用户按下向左/向右方向键，应该将光标定位到名称最前面或最后面，而不是'.'的后面，除非处于非全选状态”）
                        if (key == Qt::Key_Left) {
                            editor->setCursorPosition(0);
                        } else {
                            // 2026-07-26 极致重构：按下向右键光标一键定位到文件名基名（不含扩展名部分）的末端（点号前面）（对应用户原话：“我指的是文件名，不是后缀名...基名”）
                            QString val = editor->text();
                            int lastDot = val.lastIndexOf('.');
                            if (lastDot > 0) {
                                editor->setCursorPosition(lastDot);
                            } else {
                                editor->setCursorPosition(val.length());
                            }
                        }
                        editor->deselect(); // 清除全选高亮状态
                        keyEvent->accept();
                        return true; // 吞噬该事件，不让其触发默认定位
                    }
                    return false; // 非全选状态，走默认逐字位移
                }
            } 
        } 
        return QStyledItemDelegate::eventFilter(obj, event); 
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
        textRect.adjust(0, -1, 0, 1);
        editor->setGeometry(textRect);
    }
};

} // namespace ArcMeta
