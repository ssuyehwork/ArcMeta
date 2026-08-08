#include "ThumbnailDelegate.h"
#include "ContentPanel.h"
#include "CardPainterHelper.h"
#include "../core/ModelContract.h"
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
    if (!index.isValid()) return;

    // 双轨回收站与分组展示：如果该项为组标题，独立精美绘制
    if (index.data(IsGroupHeaderRole).toBool()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QRect rect = option.rect;
        
        // 绘制一条精美的分界线
        painter->setPen(QPen(QColor("#333333"), 1, Qt::SolidLine));
        painter->drawLine(rect.left(), rect.bottom() - 2, rect.right(), rect.bottom() - 2);

        // 绘制大标题文字，使用青蓝色/青绿色以突出主题
        QFont font("Microsoft YaHei", 11, QFont::Bold);
        painter->setFont(font);
        painter->setPen(QColor("#1abc9c"));

        QString text = index.data(Qt::DisplayRole).toString();
        // 缩进 10 像素避免紧贴边缘
        painter->drawText(rect.adjusted(10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);

        painter->restore();
        return;
    }

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

QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& /*option*/, const QModelIndex& index) const { 
    FileNameLineEdit* editor = new FileNameLineEdit(parent); 
    editor->setStyleSheet( 
        "QLineEdit {" 
        "  background-color: #2D2D2D;" 
        "  color: #FFFFFF;" 
        "  selection-background-color: #3498db;" 
        "  border: 1px solid #3498db;" 
        "  border-radius: 4px;" 
        "  padding: 0px 4px;" 
        "  margin: 0px;" 
        "  font-size: 8pt;" 
        "}" 
    ); 
 
    bool isFolder = (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category"); 
    editor->setIsFolder(isFolder); 
    editor->installEventFilter(const_cast<ThumbnailDelegate*>(this)); 
    return editor; 
} 
 
void ThumbnailDelegate::updateEditorGeometry(QWidget* editor, 
                                              const QStyleOptionViewItem& option, 
                                              const QModelIndex& /*index*/) const { 
    Metrics m = calculateMetrics(option); 
    // 根据当前视图设备的 DPI 比例动态自适应放缩微调 
    double dpr = option.widget ? option.widget->devicePixelRatio() : 1.0; 
    int offsetLeft = static_cast<int>(1 * dpr); 
    int offsetTop = static_cast<int>(5 * dpr); 
    int offsetRight = static_cast<int>(-1 * dpr); 
    int offsetBottom = static_cast<int>(-5 * dpr); 
 
    editor->setGeometry(m.textRect.adjusted(offsetLeft, offsetTop, offsetRight, offsetBottom)); 
} 
 
void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const { 
    QString value = index.model()->data(index, Qt::EditRole).toString(); 
    FileNameLineEdit* lineEdit = qobject_cast<FileNameLineEdit*>(editor);  
    if (lineEdit) { 
        lineEdit->setText(value); // 纯粹同步赋值，高亮交由 FileNameLineEdit::focusInEvent 完美同步接管 
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
