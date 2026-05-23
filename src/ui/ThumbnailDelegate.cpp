#include "ThumbnailDelegate.h"
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include <QFileInfo>
#include <QMouseEvent>
#include <QLineEdit>
#include "UiHelper.h"

namespace ArcMeta {

ThumbnailDelegate::ThumbnailDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void ThumbnailDelegate::setHasThumbnailRole(int role) { m_hasThumbnailRole = role; }
void ThumbnailDelegate::setRatingRole(int role) { m_ratingRole = role; }
void ThumbnailDelegate::setPathRole(int role) { m_pathRole = role; }
void ThumbnailDelegate::setPinnedRole(int role) { m_pinnedRole = role; }
void ThumbnailDelegate::setManagedRole(int role) { m_managedRole = role; }
void ThumbnailDelegate::setTypeRole(int role) { m_typeRole = role; }
void ThumbnailDelegate::setIsEmptyRole(int role) { m_isEmptyRole = role; }

ThumbnailDelegate::Metrics ThumbnailDelegate::calculateMetrics(const QStyleOptionViewItem& option) const {
    Metrics m;
    const int textHeight = 36;
    const int ratingHeight = 20;
    const int gap = 4;

    m.ratingH = ratingHeight;
    // 底部预留高度增加，包含星级区域和间隙
    m.cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + m.ratingH + gap + 3));
    
    // 星级坐标脱离卡片范围
    m.ratingY = m.cardRect.bottom() + gap;

    m.textRect = QRect(option.rect.left() + 3,
                       m.ratingY + m.ratingH,
                       option.rect.width() - 6,
                       textHeight);
    
    m.starSize = 18;
    m.starSpacing = -2;
    int banW = 14;
    int banGap = 2; // 保持间隙一致性
    int infoTotalW = banW + banGap + (5 * m.starSize) + (4 * m.starSpacing);
    int infoStartX = m.cardRect.left() + (m.cardRect.width() - infoTotalW) / 2;
    
    m.banRect = QRect(infoStartX, m.ratingY + (m.ratingH - banW) / 2, banW, banW);
    m.starsStartX = infoStartX + banW + banGap;

    return m;
}

void ThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Metrics m = calculateMetrics(option);
    bool isSelected = (option.state & QStyle::State_Selected);

    bool hasThumb = index.data(m_hasThumbnailRole).toBool();
    QVariant decoData = index.data(Qt::DecorationRole);
    QPixmap thumb;
    if (decoData.canConvert<QPixmap>()) {
        thumb = decoData.value<QPixmap>();
    } else if (decoData.canConvert<QIcon>()) {
        QIcon icon = decoData.value<QIcon>();
        if (!icon.isNull()) {
            thumb = icon.pixmap(m.cardRect.size());
        }
    }

    // [修正] 如果具有缩略图，则动态收缩 cardRect 以匹配实际内容，消除留白
    if (hasThumb && !thumb.isNull()) {
        QPixmap scaled = thumb.scaled(m.cardRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m.cardRect = QRect(m.cardRect.center().x() - scaled.width() / 2,
                           m.cardRect.center().y() - scaled.height() / 2,
                           scaled.width(), scaled.height());
        thumb = scaled;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 绘制卡片背景 (背景色强制为 #2d2d2d)
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor("#2d2d2d"));
    painter->drawRoundedRect(m.cardRect, 6, 6);

    // ② 内容绘制与裁剪
    painter->save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(m.cardRect, 6, 6);
    painter->setClipPath(clipPath);

    if (hasThumb && !thumb.isNull()) {
        painter->drawPixmap(m.cardRect.topLeft(), thumb);
    } else {
        QIcon icon = qvariant_cast<QIcon>(decoData);
        QPoint center = m.cardRect.center();
        if (!icon.isNull())
            icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }
    painter->restore();

    // ③ 绘制卡片边框 (选中 3px 蓝色，未选中 1px #4a4a4a)
    painter->save();
    if (isSelected) {
        painter->setPen(QPen(QColor("#3498db"), 3));
    } else {
        painter->setPen(QPen(QColor("#4a4a4a"), 1));
    }
    painter->setBrush(Qt::NoBrush);
    // 抵消画笔宽度导致的一半粗细落在矩形外的问题
    painter->drawRoundedRect(m.cardRect, 6, 6);
    painter->restore();

    // [新增] 状态位图标绘制 (置顶 vs. 已录入 互斥)
    if (m_pinnedRole != -1 && m_managedRole != -1) {
        bool isPinned = index.data(m_pinnedRole).toBool();
        bool isManaged = index.data(m_managedRole).toBool();
        if (isPinned || isManaged) {
            QRect statusRect(m.cardRect.right() - 22, m.cardRect.top() + 8, 16, 16);
            if (isPinned) {
                UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, statusRect);
            } else {
                UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, statusRect);
            }
        }
    }

    // [新增] 扩展名角标
    if (m_pathRole != -1) {
        QString path = index.data(m_pathRole).toString();
        QFileInfo info(path);
        QString ext = info.isDir() ? "DIR" : info.suffix().toUpper();
        if (ext.isEmpty()) ext = "FILE";
        QColor badgeColor = UiHelper::getExtensionColor(ext);
        QRect extRect(m.cardRect.left() + 8, m.cardRect.top() + 8, 36, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeColor);
        painter->drawRoundedRect(extRect, 2, 2);
        painter->setPen(QColor("#FFFFFF"));
        QFont extFont = painter->font(); extFont.setPointSize(8); extFont.setBold(true);
        painter->setFont(extFont);
        painter->drawText(extRect, Qt::AlignCenter, ext);
    }

    painter->restore(); // 释放裁剪区 (解除对 cardRect 的裁剪)

    // [新增] 评级星级 (现在绘制在裁剪区外，处于卡片与文件名的间隙处)
    if (m_ratingRole != -1) {
        int rating = index.data(m_ratingRole).toInt();
        bool shouldShowRating = (rating > 0) || isSelected;
        if (shouldShowRating) {
            UiHelper::getIcon("no_color", QColor("#B0B0B0"), m.banRect.width()).paint(painter, m.banRect);
            QPixmap filledStar = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(m.starSize, m.starSize), QColor("#EF9F27"));
            QPixmap emptyStar = UiHelper::getPixmap("star-rate-rating-outline-svgrepo-com.svg", QSize(m.starSize, m.starSize), QColor("#888888"));
            for (int i = 0; i < 5; ++i) {
                painter->drawPixmap(m.starRect(i), (i < rating) ? filledStar : emptyStar);
            }
        }
    }


    // ③ 文件名（卡片下方）
    painter->save();
    QString name = index.data(Qt::DisplayRole).toString();
    painter->setPen(isSelected ? QColor("#3498db") : QColor("#EEEEEE"));

    // 2026-06-xx 物理同步：针对未录入项目应用半透明效果
    if (m_managedRole != -1 && !isSelected && !index.data(m_managedRole).toBool()) {
        painter->setPen(QColor(238, 238, 238, 120));
    }

    QFont textFont = painter->font();
    textFont.setPointSize(8);
    painter->setFont(textFont);

    // 零宽空格注入以支持非标准断行（针对两行显示的潜在需求，虽然目前 elidedText 是单行）
    QString displayName = name;
    displayName.replace("_", "_\u200B");
    displayName.replace(".", ".\u200B");

    painter->drawText(m.textRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::TextWordWrap,
        option.fontMetrics.elidedText(displayName, Qt::ElideMiddle, m.textRect.width() * 2));
    painter->restore();

    // ④ [新增] 空文件夹特殊标记 (ContentPanel 移植)
    if (m_isEmptyRole != -1 && m_typeRole != -1) {
        if (index.data(m_typeRole).toString() == "folder" && index.data(m_isEmptyRole).toBool()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(QColor("#41F2F2"), 1, Qt::DashLine));
            painter->drawRoundedRect(m.cardRect, 6, 6);
            painter->restore();
        }
    }
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QStyledItemDelegate::sizeHint(option, index);
}

QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (editor) {
        // 按照用户要求：修改为项目标准蓝 (#3498db)
        editor->setStyleSheet(
            "background-color: #2D2D2D; color: white; selection-background-color: #3498db; "
            "border: 1px solid #3498db; border-radius: 4px; padding: 0 4px;"
        );
    }
    return editor;
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    Metrics m = calculateMetrics(option);
    // 修正编辑器位置，使其与文件名文字区域对齐并留出少量边距
    editor->setGeometry(m.textRect.adjusted(1, 4, -1, -4));
}

void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (lineEdit) {
        lineEdit->setText(value); 
        // 2026-06-xx 物理对标：重命名时仅选中主文件名，不含后缀
        int lastDot = value.lastIndexOf('.'); 
        if (lastDot > 0) { 
            lineEdit->setSelection(0, lastDot); 
        } else { 
            lineEdit->selectAll(); 
        }
    }
}

bool ThumbnailDelegate::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = reinterpret_cast<QKeyEvent*>(event); 
        QLineEdit* editor = qobject_cast<QLineEdit*>(obj); 
        if (editor) { 
            switch (keyEvent->key()) { 
                case Qt::Key_Left: 
                case Qt::Key_Right: 
                case Qt::Key_Up: 
                case Qt::Key_Down: 
                case Qt::Key_Home: 
                case Qt::Key_End: 
                    keyEvent->accept(); 
                    return false; 
                default: 
                    break; 
            } 
        } 
    } 
    return QStyledItemDelegate::eventFilter(obj, event); 
} 

bool ThumbnailDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (m_ratingRole != -1 && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mEvent = reinterpret_cast<QMouseEvent*>(event);
        if (mEvent->button() == Qt::LeftButton) {
            Metrics m = calculateMetrics(option);

            if (m.banRect.contains(mEvent->pos())) {
                model->setData(index, 0, m_ratingRole);
                event->accept();
                return true;
            }

            for (int i = 0; i < 5; ++i) {
                if (m.starRect(i).contains(mEvent->pos())) {
                    model->setData(index, i + 1, m_ratingRole);
                    event->accept();
                    return true;
                }
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace ArcMeta
