#include "ThumbnailDelegate.h"
#include "ContentPanel.h"
#include "CardPainterHelper.h"
#include "ElidedTextUtility.h"
#include "../meta/MetadataManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include <QFileInfo>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTimer>
#include <QAbstractItemView>
#include <QFile>
#include "UiHelper.h"
#include "ToolTipOverlay.h"

namespace ArcMeta {

ThumbnailDelegate::ThumbnailDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void ThumbnailDelegate::setHasThumbnailRole(int role) { m_hasThumbnailRole = role; }
void ThumbnailDelegate::setRatingRole(int role) { m_ratingRole = role; }
void ThumbnailDelegate::setPathRole(int role) { m_pathRole = role; }
void ThumbnailDelegate::setPinnedRole(int role) { m_pinnedRole = role; }
void ThumbnailDelegate::setManagedRole(int role) { m_managedRole = role; }
void ThumbnailDelegate::setTypeRole(int role) { m_typeRole = role; }
void ThumbnailDelegate::setIsEmptyRole(int role) { m_isEmptyRole = role; }
void ThumbnailDelegate::setColorRole(int role) { m_colorRole = role; }
void ThumbnailDelegate::setRegistrationProgressRole(int role) { m_registrationProgressRole = role; }

ThumbnailDelegate::Metrics ThumbnailDelegate::calculateMetrics(const QStyleOptionViewItem& option) const {
    Metrics m;
    const int textHeight = 36;
    const int ratingHeight = 24;
    const int gap = 4;

    m.ratingH = ratingHeight;
    // 底部预留高度增加，包含星级区域和间隙
    m.cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + m.ratingH + gap + 3));
    
    // 星级坐标脱离卡片范围
    m.ratingY = m.cardRect.bottom() + gap;

    m.textRect = QRect(option.rect.left() + 3,
                       m.ratingY + m.ratingH - 5,
                       option.rect.width() - 6,
                       textHeight);
    
    int zoom = option.decorationSize.width(); // 物理缩放级别

    m.starSize = 22;
    m.starSpacing = -4; // 2026-06-08 优化：默认间距调紧
    int banW = 14;

    // 2026-06-08 按照调试增强版 V2 优化：实现“动态比例星级”
    // 虽然底限是 96，但在接近极限 (100) 时提前缩小星级，确保视觉紧凑感
    if (zoom < 100) {
        m.starSize = 18; 
        m.starSpacing = -4;
        banW = 12;
    }

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

    // 2026-11-14 执行第三步：图形文件等待缩略图时，绘制轻量灰色占位背景
    bool isWaitingThumb = false;
    if (m_pathRole != -1 && thumb.isNull()) {
        QString path = index.data(m_pathRole).toString();
        QString ext = QFileInfo(path).suffix().toLower();
        if (UiHelper::isGraphicsFile(ext) || ext == "svg") {
            isWaitingThumb = true;
        }
    }

    bool isGrid = option.widget ? option.widget->property("gridMode").toBool() : false;

    // ① 绘制主体卡片底色及缩略图 Cover
    CardPainterHelper::drawCardCover(painter, m.cardRect, isSelected, hasThumb, thumb, 
                                     qvariant_cast<QIcon>(decoData), isGrid, isWaitingThumb);

    // ② 绘制卡片边框
    CardPainterHelper::drawCardBorder(painter, m.cardRect, isSelected);

    // ③ 绘制状态互斥标记及进度环
    if (m_pinnedRole != -1 && m_managedRole != -1) {
        bool isPinned = index.data(m_pinnedRole).toBool();
        bool isManaged = index.data(m_managedRole).toBool();
        bool isDir = index.data(m_typeRole).toString() == "folder";
        double progress = (m_registrationProgressRole != -1) ? index.data(m_registrationProgressRole).toDouble() : -1.0;

        CardPainterHelper::drawStatusIndicators(painter, m.cardRect, isPinned, isManaged, isDir, progress);
    }

    // ④ 绘制自适应扩展名徽章
    if (m_pathRole != -1) {
        QString type = (m_typeRole != -1) ? index.data(m_typeRole).toString() : "";
        QString path = index.data(m_pathRole).toString();
        QFileInfo info(path);
        QString ext;
        if (type == "category" || type == "folder") {
            ext = "DIR"; // 分类与文件夹均强制显示为 "DIR" 徽章，增强视觉一致性
        } else {
            ext = info.isDir() ? "DIR" : info.suffix().toUpper();
        }
        if (ext.isEmpty()) ext = "FILE";

        CardPainterHelper::drawExtensionBadge(painter, m.cardRect, ext, hasThumb);
    }

    // ⑤ 绘制评级星级与彩色胶囊底色
    if (m_ratingRole != -1) {
        int rating = index.data(m_ratingRole).toInt();
        QString colorStr = (m_colorRole != -1) ? index.data(m_colorRole).toString() : "";

        CardPainterHelper::drawRatingStars(painter, m.banRect, m.cardRect, m.starSize, m.starSpacing, m.ratingY, m.ratingH, m.starsStartX,
                                          rating, colorStr, isSelected);
    }

    // ⑥ 绘制截断文字 (调用私有方法处理)
    drawFileNameText(painter, m.textRect, isSelected, index, option);

    // ⑦ 绘制空文件夹特异虚线边框
    if (!isSelected && m_isEmptyRole != -1 && m_typeRole != -1) {
        if (index.data(m_typeRole).toString() == "folder" && index.data(m_isEmptyRole).toBool()) {
            CardPainterHelper::drawEmptyFolderBorder(painter, m.cardRect);
        }
    }
}

void ThumbnailDelegate::drawFileNameText(QPainter* painter, const QRect& textRect, bool isSelected, const QModelIndex& index, const QStyleOptionViewItem& option) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QString name = index.data(Qt::DisplayRole).toString();
    painter->setPen(isSelected ? QColor("#3498db") : QColor("#EEEEEE"));

    // 针对未录入项目应用半透明效果
    if (m_managedRole != -1 && !isSelected && !index.data(m_managedRole).toBool()) {
        painter->setPen(QColor(238, 238, 238, 120));
    }

    QFont textFont = painter->font();
    textFont.setPointSize(8);
    painter->setFont(textFont);

    // 调用提取的静态排版工具方法，限制文件名最多2行
    QString displayName = ElidedTextUtility::elideTwoLinesText(name, option.fontMetrics, textRect.width() - 8);
    painter->drawText(textRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::TextWordWrap, displayName);
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
    Metrics m = calculateMetrics(option);
    // 修正编辑器位置，使其与文件名文字区域对齐并留出少量边距
    // 高度降低 2 像素：通过上下各收缩 1 像素实现 (从 4 变 5)
    editor->setGeometry(m.textRect.adjusted(1, 5, -1, -5));
}

void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (lineEdit) {
        lineEdit->setText(value); 

        // 如果是文件夹或分类，全选；如果是文件，仅选中名称部分
        // 使用 QTimer::singleShot 确保在 Qt 内部默认全选逻辑之后执行，彻底解决失效问题
        bool isFolder = (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category");
        
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
}

void ThumbnailDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit) return;

    QString newName = lineEdit->text().trimmed();
    if (newName.isEmpty()) return;

    // 🚀【方案 A 核心】：仅调用标准的 setData，没有任何 parent 向上引用的非标代码！
    model->setData(index, newName, Qt::EditRole);
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

bool ThumbnailDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, 
                                const QStyleOptionViewItem& option, const QModelIndex& index) {
    Metrics m = calculateMetrics(option);
    QRect statusRect(m.cardRect.right() - 22, m.cardRect.top() + 8, 16, 16);

    if (statusRect.contains(event->pos())) {
        double p = (m_registrationProgressRole != -1) ? index.data(m_registrationProgressRole).toDouble() : -1.0;
        if (p >= 0.0) {
            ToolTipOverlay::instance()->showText(event->globalPos(), 
                QString("登记进度: %1%").arg(qRound(p * 100)), 0);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

} // namespace ArcMeta
