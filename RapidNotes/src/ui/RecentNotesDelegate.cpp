#include "RecentNotesDelegate.h"
#include <QFileInfo>

RecentNotesDelegate::RecentNotesDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize RecentNotesDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QSize(option.rect.width(), 45); // 紧凑型高度
}

bool RecentNotesDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event && event->type() == QEvent::ToolTip && index.isValid()) {
        QString tip = index.data(Qt::ToolTipRole).toString();
        if (!tip.isEmpty()) {
            ToolTipOverlay::instance()->showText(event->globalPos(), tip, 2000);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

void RecentNotesDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (!index.isValid()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect;
    bool isSelected = (option.state & QStyle::State_Selected);
    bool isHovered = (option.state & QStyle::State_MouseOver);

    // 1. 绘制基础背景 (斑马纹)
    QColor bgColor = (index.row() % 2 == 0) ? QColor("#1E1E1E") : QColor("#181818");
    painter->fillRect(rect, bgColor);

    // 2. 绘制高亮背景 (选中或悬停)
    if (isSelected || isHovered) {
        QColor highlightColor("#4a90e2"); // 固定蓝色，不依赖 QuickWindow
        QColor bg = isSelected ? highlightColor : QColor(255, 255, 255);
        bg.setAlpha(isSelected ? 15 : 20);
        painter->fillRect(rect, bg);
    }

    // 3. 绘制选中指示条 (左侧蓝色条)
    if (isSelected) {
        painter->fillRect(QRect(rect.left(), rect.top(), 5, rect.height()), QColor("#4a90e2"));
    }

    // 4. 分隔线
    painter->setPen(QColor(0, 0, 0, 25));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // 5. 图标
    QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) {
        QString type = index.data(NoteModel::TypeRole).toString();
        if (type == "image") {
            icon.paint(painter, rect.left() + 7, rect.top() + (rect.height() - 32) / 2, 32, 32);
        } else {
            icon.paint(painter, rect.left() + 13, rect.top() + (rect.height() - 20) / 2, 20, 20);
        }
    }

    // 6. 标题文本
    QString text = index.data(NoteModel::TitleRole).toString();
    QString type = index.data(NoteModel::TypeRole).toString();
    QString content = index.data(NoteModel::ContentRole).toString();
    
    if (type == "file" || type == "files") {
        QStringList paths = content.split(';', Qt::SkipEmptyParts);
        if (!paths.isEmpty()) {
            QString firstExt = QFileInfo(paths.first().trimmed()).suffix().toUpper();
            bool sameExt = true;
            for (const QString& p : paths) {
                if (QFileInfo(p.trimmed()).suffix().toUpper() != firstExt) {
                    sameExt = false;
                    break;
                }
            }
            if (sameExt && !firstExt.isEmpty()) {
                text = QString("[%1] %2").arg(firstExt, text);
            }
        }
    }

    painter->setPen(isSelected ? Qt::white : QColor("#CCCCCC"));
    painter->setFont(QFont("Microsoft YaHei", 9));
    QRect textRect = rect.adjusted(40, 0, -70, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, 
                     painter->fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));

    // 7. 时间显示
    QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("MM-dd HH:mm");
    painter->setPen(QColor("#666666"));
    painter->setFont(QFont("Segoe UI", 7));
    painter->drawText(rect.adjusted(0, 3, -10, 0), Qt::AlignRight | Qt::AlignTop, timeStr);

    // 8. 星级与收藏
    bool isFavorite = index.data(NoteModel::FavoriteRole).toBool();
    int rating = index.data(NoteModel::RatingRole).toInt();
    
    if (isFavorite || rating > 0) {
        int starSize = 9;
        int spacing = -1;
        int displayRating = qMin(rating, 5);
        int numIcons = (isFavorite ? 1 : 0) + displayRating;
        int totalWidth = (numIcons > 0) ? (numIcons * starSize + (numIcons - 1) * spacing) : 0;
        
        int currentX = rect.right() - 9 - totalWidth;
        int currentY = rect.bottom() - starSize - 5;

        if (isFavorite) {
            QIcon bookmarkIcon = IconHelper::getIcon("bookmark_filled", "#2ECC71", starSize);
            bookmarkIcon.paint(painter, QRect(currentX, currentY, starSize, starSize));
            currentX += (starSize + spacing);
        }

        if (displayRating > 0) {
            QIcon starFilled = IconHelper::getIcon("star_filled", "#F1C40F", starSize);
            for (int i = 0; i < displayRating; ++i) {
                starFilled.paint(painter, QRect(currentX, currentY, starSize, starSize));
                currentX += (starSize + spacing);
            }
        }
    }

    painter->restore();
}
