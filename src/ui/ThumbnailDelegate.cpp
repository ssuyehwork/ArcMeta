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

ThumbnailDelegate::Metrics ThumbnailDelegate::calculateMetrics(const QStyleOptionViewItem& option) const {
    Metrics m;
    const int textHeight = 36;
    m.cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + 3));
    m.textRect = QRect(option.rect.left() + 3,
                       option.rect.bottom() - textHeight,
                       option.rect.width() - 6,
                       textHeight);

    m.ratingH = 16;
    m.ratingY = m.cardRect.bottom() - m.ratingH - 4;
    m.starSize = 14;
    m.starSpacing = 2;
    int banW = 12;
    int banGap = 4;
    int infoTotalW = banW + banGap + (5 * m.starSize) + (4 * m.starSpacing);
    int infoStartX = m.cardRect.left() + (m.cardRect.width() - infoTotalW) / 2;

    m.banRect = QRect(infoStartX, m.ratingY + (m.ratingH - banW) / 2, banW, banW);
    m.starsStartX = infoStartX + banW + banGap;

    return m;
}

void ThumbnailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Metrics m = calculateMetrics(option);
    bool isSelected = (option.state & QStyle::State_Selected);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // ① 圆角裁剪（仅作用于卡片）
    QPainterPath clipPath;
    clipPath.addRoundedRect(m.cardRect, 6, 6);
    painter->setClipPath(clipPath);

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

    if (hasThumb && !thumb.isNull()) {
        QPixmap scaled = thumb.scaled(m.cardRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = m.cardRect.center().x() - scaled.width() / 2;
        int y = m.cardRect.center().y() - scaled.height() / 2;
        painter->drawPixmap(x, y, scaled);
    } else {
        painter->fillRect(m.cardRect, QColor("#2D2D2D"));
        QIcon icon = qvariant_cast<QIcon>(decoData);
        QPoint center = m.cardRect.center();
        if (!icon.isNull())
            icon.paint(painter, QRect(center.x() - 24, center.y() - 24, 48, 48));
    }

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

    // [新增] 评级星级
    if (m_ratingRole != -1) {
        int rating = index.data(m_ratingRole).toInt();
        bool shouldShowRating = (rating > 0) || isSelected;
        if (shouldShowRating) {
            // 在图片下方绘制半透明背景，确保星级清晰
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 100));
            painter->drawRect(m.cardRect.left(), m.ratingY - 2, m.cardRect.width(), m.ratingH + 4);

            UiHelper::getIcon("no_color", QColor("#B0B0B0"), m.banRect.width()).paint(painter, m.banRect);
            QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(m.starSize, m.starSize), QColor("#EF9F27"));
            QPixmap emptyStar = UiHelper::getPixmap("star", QSize(m.starSize, m.starSize), QColor("#888888"));
            for (int i = 0; i < 5; ++i) {
                QRect starRect(m.starsStartX + i * (m.starSize + m.starSpacing), m.ratingY + (m.ratingH - m.starSize) / 2, m.starSize, m.starSize);
                painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
            }
        }
    }

    painter->restore(); // 释放裁剪区

    // ② 选中边框（在裁剪区外绘制，确保完整显示）
    if (isSelected) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor("#3498db"), 2)); 
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m.cardRect, 6, 6);
        painter->restore();
    }

    // ③ 文件名（卡片下方）
    painter->save();
    painter->setPen(isSelected ? QColor("#3498db") : QColor("#C8C8C8"));
    painter->drawText(m.textRect, Qt::AlignHCenter | Qt::AlignVCenter,
        option.fontMetrics.elidedText(
            index.data(Qt::DisplayRole).toString(),
            Qt::ElideMiddle, m.textRect.width()));
    painter->restore();
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
    const int textHeight = 36;
    // 按照用户要求：修正编辑器位置。
    // 计算文字区域：位于整体区域底部 textHeight 像素
    QRect textRect(option.rect.left() + 4,
                   option.rect.bottom() - textHeight,
                   option.rect.width() - 8,
                   textHeight - 4);
    editor->setGeometry(textRect);
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
                QRect starRect(m.starsStartX + i * (m.starSize + m.starSpacing), m.ratingY + (m.ratingH - m.starSize) / 2, m.starSize, m.starSize);
                if (starRect.contains(mEvent->pos())) {
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
