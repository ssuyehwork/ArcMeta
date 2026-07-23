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
            // 自定义绘制名称列与最左侧圆角卡片（最左侧看片）
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);

            int padding = 3;
            int side = option.rect.height() - (padding * 2);
            if (side <= 0) side = 16;

            // 微卡片矩形区域
            QRect squareRect(option.rect.left() + 6, option.rect.top() + padding, side, side);

            // 1. 绘制 4px 圆角微型卡片容器背景（透明背景穿透，对应用户原话：“卡片的背景色都必须是透明的”）
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::transparent);
            QPainterPath cardPath;
            cardPath.addRoundedRect(squareRect, 4, 4);
            painter->drawPath(cardPath);

            // 2. 图像/图标平滑居中绘制（最左侧看片核心逻辑）
            QVariant decoData = index.data(Qt::DecorationRole);
            bool hasThumb = index.data(HasThumbnailRole).toBool();

            if (hasThumb) {
                QPixmap thumb;
                if (decoData.canConvert<QPixmap>()) {
                    thumb = decoData.value<QPixmap>();
                } else if (decoData.canConvert<QIcon>()) {
                    QIcon icon = decoData.value<QIcon>();
                    if (!icon.isNull()) {
                        thumb = icon.pixmap(squareRect.size());
                    }
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
                        int iconSize = qRound(side * 0.6);
                        QRect iconRect(squareRect.center().x() - iconSize / 2,
                                       squareRect.center().y() - iconSize / 2,
                                       iconSize, iconSize);
                        icon.paint(painter, iconRect);
                    }
                }
            } else {
                QIcon icon = qvariant_cast<QIcon>(decoData);
                if (!icon.isNull()) {
                    int iconSize = qRound(side * 0.6);
                    QRect iconRect(squareRect.center().x() - iconSize / 2,
                                   squareRect.center().y() - iconSize / 2,
                                   iconSize, iconSize);
                    icon.paint(painter, iconRect);
                }
            }

            // 3. 文本排版向右偏移并采用中间省略
            QString name = index.data(Qt::DisplayRole).toString();
            QColor textColor = selected ? QColor("#FFFFFF") : QColor("#EEEEEE");

            painter->setPen(textColor);
            painter->setFont(option.font);

            QRect textRect = option.rect;
            textRect.setLeft(squareRect.right() + 10);

            QString elidedText = option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width() - 10);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

            painter->restore();
        } else if (col == 1 || col == 2 || col == 3) {
            // 这三列不调用默认 paint，完全自定义
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            if (col == 1) { // 状态列
                QModelIndex idx0 = index.model()->index(index.row(), 0);
                bool isPinned = idx0.data(IsLockedRole).toBool();
                bool isManaged = idx0.data(ManagedRole).toBool();
                bool isDir = idx0.data(TypeRole).toString() == "folder";
                double progress = idx0.data(RegistrationProgressRole).toDouble();

                QRect iconRect(option.rect.left() + (option.rect.width() - 16) / 2,
                               option.rect.top() + (option.rect.height() - 16) / 2, 16, 16);

                if (isPinned) {
                    UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, iconRect);
                } else if (isDir && progress >= 0.0 && progress < 1.0) {
                    // --- 绘制进度环 (开箱即用代码) --- 
                    // 2026-07-xx 按照 Development_Plan 3.1：进度弧线完全通过数据库中的 0 和 1 标记值计算得出
                    painter->save(); 
                    painter->setRenderHint(QPainter::Antialiasing); 
                    painter->setPen(QPen(QColor(60, 60, 60, 180), 2)); 
                    painter->drawEllipse(iconRect.adjusted(1, 1, -1, -1)); 
                    QPen pPen(QColor("#3498db"), 2); 
                    pPen.setCapStyle(Qt::RoundCap); 
                    painter->setPen(pPen); 
                    int spanAngle = -qRound(progress * 360 * 16); 
                    painter->drawArc(iconRect.adjusted(1, 1, -1, -1), 90 * 16, spanAngle); 
                    painter->restore(); 
                } else if (isManaged || (isDir && progress >= 1.0)) {
                    UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, iconRect);
                }
            } else if (col == 2) { // 星级列
                // 2026-06-16 按照方案 20 纠偏：仅在选中或评分 > 0 时显示图标，减少视觉干扰
                QModelIndex idx0 = index.model()->index(index.row(), 0);
                int rating = idx0.data(RatingRole).toInt();
                bool isSelected = option.state & QStyle::State_Selected;

                if (rating > 0 || isSelected) {
                    QString colorName = idx0.data(ColorRole).toString();

                    int banW = 12;            // 禁止图标宽度：对齐紧凑规格 12px
                    int starSize = 18;        // 星级图标尺寸：18px
                    int banGap = 2;           // 禁止图标与第 1 颗星的间距：2px
                    int starSpacing = -4;     // 星星与星星之间的间距：-4px
                    int startX = option.rect.left() + 6;

                    QRect banRect(startX, option.rect.top() + (option.rect.height() - banW) / 2, banW, banW);
                    int starsStartX = startX + banW + banGap;

                    // 若存在颜色标记，先在星级下方绘制一行半圆角背景胶囊
                    if (!colorName.isEmpty()) {
                        QColor bgColor = UiHelper::parseColorName(colorName);
                        if (bgColor.isValid()) {
                            painter->save();
                            painter->setBrush(bgColor);
                            painter->setPen(Qt::NoPen);
                            QRect lastStarRect(starsStartX + 4 * (starSize + starSpacing), option.rect.top() + (option.rect.height() - starSize) / 2, starSize, starSize);
                            QRect totalRect = banRect.united(lastStarRect);
                            painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
                            painter->restore();
                        }
                    }

                    // 移植网格视图的亮度对比度感知算法
                    QColor bgColor = colorName.isEmpty() ? QColor(0,0,0,0) : UiHelper::parseColorName(colorName);
                    double luminance = 0.0;
                    if (bgColor.isValid() && bgColor.alpha() > 0) {
                        luminance = (0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue()) / 255.0;
                    }

                    QColor starColor, emptyStarColor;
                    if (colorName.isEmpty()) {
                        starColor      = QColor("#CCCCCC");
                        emptyStarColor = QColor("#888888");
                    } else if (luminance < 0.5) {
                        starColor      = QColor("#FFFFFF");
                        emptyStarColor = QColor(255, 255, 255, 160);
                    } else {
                        starColor      = QColor("#1A1A1A");
                        emptyStarColor = QColor(0, 0, 0, 140);
                    }

                    // 统一物理排版与标准 SVG 图标绘制 (对齐网格视图原生图标 star_filled 和 star)
                    QIcon banIcon = UiHelper::getIcon("no_color", starColor, banW);
                    banIcon.paint(painter, banRect);

                    QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), starColor);
                    QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), emptyStarColor);

                    for (int i = 0; i < 5; ++i) {
                        QRect starRect(starsStartX + i * (starSize + starSpacing), option.rect.top() + (option.rect.height() - starSize) / 2, starSize, starSize);
                        painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
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

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;
        QString value = lineEdit->text();
        if (value.isEmpty() || value == index.data(Qt::DisplayRole).toString()) return;

        // 2026-06-xx 架构解耦修复：物理重命名职责已彻底移至 Model 层的 setData。
        // Delegate 仅负责触发数据变更。这消除了“重复重命名”导致的静默失败 Bug。
        if (model->setData(index, value, Qt::EditRole)) {
            // 2026-xx-xx 按照用户要求：触发刷新信号
            // 物理修复：editor->parent() 返回 QObject*，需先转为 QWidget*
            QAbstractItemView* view = qobject_cast<QAbstractItemView*>(editor->parentWidget()->parentWidget());
            if (view) {
                // 对于 TreeView，editor 的 parent 是 viewport，viewport 的 parent 是 TreeView
                // 向上寻找 ContentPanel 以调用 onSelectionChanged
                QWidget* p = view->parentWidget();
                while (p) {
                    ContentPanel* cp = qobject_cast<ContentPanel*>(p);
                    if (cp) {
                        cp->onSelectionChanged();
                        break;
                    }
                    p = p->parentWidget();
                }
            }
        }
    }

public:
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QStyledItemDelegate::createEditor(parent, option, index);
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
