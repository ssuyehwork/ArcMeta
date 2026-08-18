#include "QuickNoteDelegate.h"
#include "QuickWindow.h"
#include <QFileInfo>

QuickNoteDelegate::QuickNoteDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize QuickNoteDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QSize(option.rect.width(), 45); // 紧凑型高度
}


void QuickNoteDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (!index.isValid()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect;
    bool isSelected = (option.state & QStyle::State_Selected);
    bool isHovered = (option.state & QStyle::State_MouseOver);

    // 1. 绘制基础背景 (斑马纹)
    QColor bgColor = (index.row() % 2 == 0) ? QColor("#1E1E1E") : QColor("#181818");
    painter->fillRect(rect, bgColor);

    // 2. 绘制高亮背景 (选中或悬停) - 2026-04-xx 按照用户指正：彻底移除圆角脑补，还原为纯直角填充
    if (isSelected || isHovered) {
        QColor highlightColor("#4a90e2"); // 默认蓝
        QuickWindow* win = qobject_cast<QuickWindow*>(parent());
        if (win) {
            QString c = win->currentCategoryColor();
            if (!c.isEmpty() && QColor::isValidColorName(c)) highlightColor = QColor(c);
        }

        QColor bg = isSelected ? highlightColor : QColor(255, 255, 255);
        bg.setAlpha(isSelected ? 15 : 20); // 选中态背景半透明度降至 15 (约 6%)，悬停态保持 20

        // 还原为整行直角填充
        painter->fillRect(rect, bg);
    }

    // 3. 绘制指示条 (置顶与分类颜色并存逻辑)
    bool isPinned = index.data(NoteModel::PinnedRole).toBool();
    // 2026-05-04 修复：在"最近访问"视图中隐藏置顶指示条，避免逻辑混淆
    QuickWindow* win = qobject_cast<QuickWindow*>(parent());
    bool isRecentlyVisited = false;
    if (win) {
        // 通过检查当前视图类型来判断是否为"最近访问"
        // 需要添加一个方法来获取当前视图类型
        isRecentlyVisited = win->isRecentlyVisitedView();
    }
    
    if (isPinned && !isRecentlyVisited) {
        // 2026-03-12 按照用户要求，置顶指示条颜色为橙色 (#FF551C)
        // 修正：置顶线宽度统一为 1px (无论是否选中)
        painter->fillRect(QRect(rect.left(), rect.top(), 1, rect.height()), QColor("#FF551C"));
    }

    if (isSelected) {
        QColor highlightColor("#4a90e2"); // 默认蓝
        QuickWindow* win = qobject_cast<QuickWindow*>(parent());
        if (win) {
            QString c = win->currentCategoryColor();
            if (!c.isEmpty() && QColor::isValidColorName(c)) highlightColor = QColor(c);
        }

        if (isPinned && !isRecentlyVisited) {
            // 置顶项被选中：在 1px 橙色条右侧绘制 4px 分类色 (总宽 5px)
            painter->fillRect(QRect(rect.left() + 1, rect.top(), 4, rect.height()), highlightColor);
        } else {
            // 未置顶项被选中：在最左侧绘制完整的 5px 分类色
            painter->fillRect(QRect(rect.left(), rect.top(), 5, rect.height()), highlightColor);
        }
    }

    // 2. 分隔线 (对齐 Python 版，使用极浅的黑色半透明)
    painter->setPen(QColor(0, 0, 0, 25));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // 图标 (DecorationRole) - 中心点对齐在 23px
    QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) {
        QString type = index.data(NoteModel::TypeRole).toString();
        if (type == "image") {
            // 缩略图 (32x32): 起始 7px + 32/2 = 23px 中心
            icon.paint(painter, rect.left() + 7, rect.top() + (rect.height() - 32) / 2, 32, 32);
        } else {
            // SVG图标 (20x20): 起始 13px + 20/2 = 23px 中心
            icon.paint(painter, rect.left() + 13, rect.top() + (rect.height() - 20) / 2, 20, 20);
        }
    }

    // 标题文本与后缀名处理
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
    
    // 调整右侧边距 (-70) 以避开右侧的时间戳和星级
    QRect textRect = rect.adjusted(40, 0, -70, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, 
                     painter->fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));

    // 文件数量数字显示 (#FF4858)
    if (type == "file" || type == "files") {
        QStringList paths = content.split(';', Qt::SkipEmptyParts);
        if (paths.size() > 1) {
            painter->save();
            painter->setPen(QColor("#FF4858"));
            // 对齐时间戳字体：Segoe UI, 7pt, 常规粗细
            painter->setFont(QFont("Segoe UI", 7));
            // 在时间戳下方 (y=15 左右) 绘制
            painter->drawText(rect.adjusted(0, 15, -10, 0), Qt::AlignRight | Qt::AlignTop, QString::number(paths.size()));
            painter->restore();
        }
    }

    // 时间 (极简展示) - 显示在右上方
    QString timeStr = index.data(NoteModel::TimeRole).toDateTime().toString("MM-dd HH:mm");
    painter->setPen(QColor("#666666"));
    painter->setFont(QFont("Segoe UI", 7));
    painter->drawText(rect.adjusted(0, 3, -10, 0), Qt::AlignRight | Qt::AlignTop, timeStr);

    // 星级 (Rating) 与 收藏 (Favorite) - 显示在右下方
    bool isFavorite = index.data(NoteModel::FavoriteRole).toBool();
    int rating = index.data(NoteModel::RatingRole).toInt();
    
    if (isFavorite || rating > 0) {
        int starSize = 9;
        int spacing = -1; // 2026-04-18 按照用户要求：拉近星级间距（视觉约 0.5-1px）
        
        // 限制最大星级为 5
        int displayRating = qMin(rating, 5);
        
        // 计算绘制总宽度：所有图标（收藏 + 星级）统一间距步进
        int numIcons = (isFavorite ? 1 : 0) + displayRating;
        int totalWidth = (numIcons > 0) ? (numIcons * starSize + (numIcons - 1) * spacing) : 0;
        
        int currentX = rect.right() - 9 - totalWidth; // 右边距保持 9
        int currentY = rect.bottom() - starSize - 5;  // 下边距保持 5

        // 1. 绘制收藏图标 (翠绿色 #2ECC71)
        if (isFavorite) {
            QIcon bookmarkIcon = IconHelper::getIcon("bookmark_filled", "#2ECC71", starSize);
            bookmarkIcon.paint(painter, QRect(currentX, currentY, starSize, starSize));
            currentX += (starSize + spacing);
        }

        // 2. 绘制星级 (金色)
        if (displayRating > 0) {
            // 2026-05-03 按照用户要求：修正图标调用错误，将 Favorite_filled 改为 star_filled
            QIcon starFilled = IconHelper::getIcon("star_filled", "#F1C40F", starSize);
            for (int i = 0; i < displayRating; ++i) {
                starFilled.paint(painter, QRect(currentX, currentY, starSize, starSize));
                currentX += (starSize + spacing);
            }
        }
    }

    painter->restore();
}
