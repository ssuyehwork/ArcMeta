#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include "ContentPanel.h"
#include "../meta/MetadataManager.h"
#include "UiHelper.h"
#include "CardPainterHelper.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;

namespace ArcMeta {

/**
 * @brief 通用树形视图代理，提供圆角高亮效果
 */
class TreeItemDelegate : public QStyledItemDelegate {
public:
    explicit TreeItemDelegate(QObject* parent = nullptr, bool showStatus = true, bool drawMiniCards = false)
        : QStyledItemDelegate(parent), m_showStatus(showStatus), m_drawMiniCards(drawMiniCards) {}
    
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
        if (index.column() >= 1) {
            opt.displayAlignment = Qt::AlignCenter;
        }
        opt.state &= ~QStyle::State_Selected;
        opt.state &= ~QStyle::State_MouseOver;
        if (selected || hover) {
            opt.features &= ~QStyleOptionViewItem::Alternate;
            opt.backgroundBrush = QBrush();
        }
        
        if (selected) {
            opt.palette.setColor(QPalette::Text, Qt::white);
        } else if (m_showStatus) {
            // 2026-06-xx 按照视觉要求：未录入项文字半透明暗淡处理
            // 物理修复：校准作用域
            bool isManaged = index.data(ManagedRole).toBool();
            if (!isManaged) {
                opt.palette.setColor(QPalette::Text, QColor(238, 238, 238, 120));
            }
        }

        // 2026-06-16 按照 8 列架构重构：第 1, 2, 3 列由代理独立绘制；第 0 列作为名称列，具有微型圆角卡片预览（最左侧看片）
        int col = index.column();
        if (col == 0 && m_drawMiniCards) {
            // 自定义绘制名称列与最左侧圆角卡片
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);

            int padding = 3;
            int side = option.rect.height() - (padding * 2);
            if (side <= 0) side = 16;

            QRect squareRect(option.rect.left() + 6, option.rect.top() + padding, side, side);

            // 1. 绘制微型卡片背景
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::transparent);
            QPainterPath cardPath;
            cardPath.addRoundedRect(squareRect, 4, 4);
            painter->drawPath(cardPath);

            // 2. 图像/图标平滑居中绘制
            QVariant decoData = index.data(Qt::DecorationRole);
            bool hasThumb = index.data(HasThumbnailRole).toBool();

            if (hasThumb) {
                QPixmap thumb;
                if (decoData.canConvert<QPixmap>()) {
                    thumb = decoData.value<QPixmap>();
                } else if (decoData.canConvert<QIcon>()) {
                    QIcon icon = decoData.value<QIcon>();
                    if (!icon.isNull()) thumb = icon.pixmap(squareRect.size());
                }

                if (!thumb.isNull()) {
                    painter->save();
                    QPainterPath clipPath;
                    clipPath.addRoundedRect(squareRect, 4, 4);
                    painter->setClipPath(clipPath);

                    QPixmap scaled = thumb.scaled(squareRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    int x = squareRect.center().x() - scaled.width() / 2;
                    int y = squareRect.center().y() - scaled.height() / 2;
                    painter->drawPixmap(x, y, scaled);

                    painter->restore();
                } else {
                    QIcon icon = qvariant_cast<QIcon>(decoData);
                    if (!icon.isNull()) {
                        int iconSize = qRound(side * 0.65);
                        QRect iconRect(squareRect.center().x() - iconSize / 2,
                                       squareRect.center().y() - iconSize / 2,
                                       iconSize, iconSize);
                        // 🚨 物理修复 ②：传入 Qt::AlignCenter，强制占位符图标在微卡片内部绝对居中！
                        icon.paint(painter, iconRect, Qt::AlignCenter);
                    }
                }
            } else {
                QIcon icon = qvariant_cast<QIcon>(decoData);
                if (!icon.isNull()) {
                    int iconSize = qRound(side * 0.65);
                    QRect iconRect(squareRect.center().x() - iconSize / 2,
                                   squareRect.center().y() - iconSize / 2,
                                   iconSize, iconSize);
                    // 🚨 物理修复 ②：传入 Qt::AlignCenter，强制占位符图标在微卡片内部绝对居中！
                    icon.paint(painter, iconRect, Qt::AlignCenter);
                }
            }

            // 3. 文本排版向右偏移
            QString name = index.data(Qt::DisplayRole).toString();
            QColor textColor = selected ? QColor("#FFFFFF") : QColor("#EEEEEE");

            painter->setPen(textColor);
            painter->setFont(option.font);

            QRect textRect = option.rect;
            textRect.setLeft(squareRect.right() + 10);

            QString elidedText = option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width() - 10);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

            painter->restore();
        } else if (col == 1 || col == 2) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            QModelIndex idx0 = index.model()->index(index.row(), 0);

            if (col == 1) { // 🚨 物理修复 ①：状态列图标在单元格内部 100% 水平+垂直绝对居中！
                bool isPinned = idx0.data(IsLockedRole).toBool();
                bool isManaged = idx0.data(ManagedRole).toBool();
                bool isDir = idx0.data(TypeRole).toString() == "folder";
                double progress = idx0.data(RegistrationProgressRole).toDouble();

                int iconSize = 16;
                // 计算单元格物理中心坐标
                QRect centeredRect(option.rect.left() + (option.rect.width() - iconSize) / 2,
                                   option.rect.top() + (option.rect.height() - iconSize) / 2,
                                   iconSize, iconSize);

                if (isPinned) {
                    UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, centeredRect, Qt::AlignCenter);
                } else if (isDir && progress >= 0.0 && progress < 1.0) {
                    painter->save(); 
                    painter->setRenderHint(QPainter::Antialiasing); 
                    painter->setPen(QPen(QColor(60, 60, 60, 180), 2)); 
                    painter->drawEllipse(centeredRect.adjusted(1, 1, -1, -1)); 
                    QPen pPen(QColor("#3498db"), 2); 
                    pPen.setCapStyle(Qt::RoundCap); 
                    painter->setPen(pPen); 
                    int spanAngle = -qRound(progress * 360 * 16); 
                    painter->drawArc(centeredRect.adjusted(1, 1, -1, -1), 90 * 16, spanAngle); 
                    painter->restore(); 
                } else if (isManaged || (isDir && progress >= 1.0)) {
                    UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, centeredRect, Qt::AlignCenter);
                }
            } else if (col == 2) { // 星级列
                int rating = idx0.data(RatingRole).toInt();
                bool isSelected = option.state & QStyle::State_Selected;
                QString colorName = idx0.data(ColorRole).toString();

                if (rating > 0 || isSelected || !colorName.isEmpty()) {
                    int banW = 12;
                    int starSize = 18;
                    int banGap = 2;
                    int starSpacing = -4;
                    int totalW = banW + banGap + 5 * starSize + 4 * starSpacing;
                    int startX = option.rect.left() + (option.rect.width() - totalW) / 2;

                    QRect banRect(startX, option.rect.top() + (option.rect.height() - banW) / 2, banW, banW);
                    int starsStartX = startX + banW + banGap; 

                    // 2. 一行代码委托绘制 5 星与彩色胶囊背景（含感知对比度自动计算）
                    CardPainterHelper::drawRatingStars(painter, banRect, option.rect, starSize, starSpacing, 
                                                      option.rect.top(), option.rect.height(), starsStartX,
                                                      rating, colorName, isSelected);
                }
            }
            painter->restore();
        } else {
            QStyledItemDelegate::paint(painter, opt, index);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;

        QString newName = lineEdit->text().trimmed();
        if (newName.isEmpty()) return;

        // 🚀【方案 A 核心】：仅调用标准的 setData，没有任何 parent 向上引用的非标代码！
        model->setData(index, newName, Qt::EditRole);
    }

public:
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        QLineEdit* editor = new QLineEdit(parent);
        // 2026-07-26 极致重构：应用精致的暗黑带蓝边框样式（背景 `#2D2D2D`，外框 `#3498db`，圆角 `4px`），消除默认白色粗糙样式
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  selection-background-color: #3498db;"
            "  border: 1px solid #3498db;"
            "  border-radius: 4px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "  font-size: 8pt;"
            "}"
        );
        editor->installEventFilter(const_cast<TreeItemDelegate*>(this));
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QRect targetRect = option.rect;

        // 2026-07-26 极致重构：如果渲染了微卡片，则对编辑器 left 边界执行完全的物理避让（对应用户原话：“绝不覆盖、叠占卡片”）
        if (index.column() == 0 && m_drawMiniCards) {
            int padding = 3;
            int side = option.rect.height() - (padding * 2);
            if (side <= 0) side = 16;
            QRect squareRect(option.rect.left() + 6, option.rect.top() + padding, side, side);
            targetRect.setLeft(squareRect.right() + 10);
        } else {
            targetRect.adjust(6, 0, -6, 0);
        }

        // 2026-07-26 极致重构：行内编辑框物理最大高度不超过 28 像素限幅约束，居中收缩留白（对应用户原话：“行内编辑的编辑框高度不可大于28像素”）
        const int maxH = 28;
        if (targetRect.height() > maxH) {
            int diff = targetRect.height() - maxH;
            int topAdj = diff / 2;
            int botAdj = diff - topAdj;
            targetRect.adjust(0, topAdj, 0, -botAdj);
        } else {
            targetRect.adjust(0, 2, 0, -2);
        }

        editor->setGeometry(targetRect);
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

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;

        lineEdit->setText(value);

        // 2026-xx-xx 按照用户要求：仅选中不含扩展名的部分
        // 物理修复：使用 QTimer 确保在 Qt 默认 selectAll 之后执行，防止逻辑被覆盖
        bool isFolder = (index.data(TypeRole).toString() == "folder" || index.data(TypeRole).toString() == "category");
        QTimer::singleShot(0, lineEdit, [lineEdit, value, isFolder]() {
            if (!lineEdit) return;
            if (isFolder) {
                lineEdit->selectAll();
            } else {
                int lastDot = value.lastIndexOf('.');
                if (lastDot > 0) {
                    lineEdit->setSelection(0, lastDot);
                } else {
                    lineEdit->selectAll();
                }
            }
        });
    }

private:
    bool m_showStatus;
    bool m_drawMiniCards;
};

} // namespace ArcMeta
