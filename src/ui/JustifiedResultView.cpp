#include "JustifiedResultView.h"
#include "DropJustifiedView.h"
#include "ContentPanel.h"
#include "UiHelper.h"
#include "ToolTipOverlay.h"
#include "../core/ModelContract.h"
#include <QPalette>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QStyleOptionViewItem>
#include <QFileInfo>
#include <QLineEdit>
#include <QTimer>
#include <QTextLayout>
#include <QTextOption>
#include <QKeyEvent>

namespace ArcMeta {

namespace {

class JustifiedThumbnailDelegate : public QStyledItemDelegate {
public:
    explicit JustifiedThumbnailDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setHasThumbnailRole(int role) { m_hasThumbnailRole = role; }
    void setRatingRole(int role) { m_ratingRole = role; }
    void setPathRole(int role) { m_pathRole = role; }
    void setPinnedRole(int role) { m_pinnedRole = role; }
    void setManagedRole(int role) { m_managedRole = role; }
    void setTypeRole(int role) { m_typeRole = role; }
    void setIsEmptyRole(int role) { m_isEmptyRole = role; }
    void setColorRole(int role) { m_colorRole = role; }
    void setRegistrationProgressRole(int role) { m_registrationProgressRole = role; }

    struct Metrics {
        QRect cardRect;
        QRect textRect;
        QRect banRect;
        int starsStartX;
        int starSize;
        int starSpacing;
        int ratingY;
        int ratingH;

        QRect starRect(int index) const {
            return QRect(starsStartX + index * (starSize + starSpacing),
                         ratingY + (ratingH - starSize) / 2,
                         starSize, starSize);
        }
    };

    Metrics calculateMetrics(const QStyleOptionViewItem& option) const {
        Metrics m;
        const int textHeight = 36;
        const int ratingHeight = 24;
        const int gap = 4;

        m.ratingH = ratingHeight;
        m.cardRect = option.rect.adjusted(3, 3, -3, -(textHeight + m.ratingH + gap + 3));
        m.ratingY = m.cardRect.bottom() + gap;

        m.textRect = QRect(option.rect.left() + 3,
                           m.ratingY + m.ratingH - 5,
                           option.rect.width() - 6,
                           textHeight);

        int zoom = option.decorationSize.width();

        m.starSize = 22;
        m.starSpacing = -4;
        int banW = 14;

        if (zoom < 100) {
            m.starSize = 18;
            m.starSpacing = -4;
            banW = 12;
        }

        int banGap = 2;
        int infoTotalW = banW + banGap + (5 * m.starSize) + (4 * m.starSpacing);
        int infoStartX = m.cardRect.left() + (m.cardRect.width() - infoTotalW) / 2;

        m.banRect = QRect(infoStartX, m.ratingY + (m.ratingH - banW) / 2, banW, banW);
        m.starsStartX = infoStartX + banW + banGap;

        return m;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Metrics m = calculateMetrics(option);
        bool isSelected = (option.state & QStyle::State_Selected);

        bool hasThumb = (m_hasThumbnailRole != -1) ? index.data(m_hasThumbnailRole).toBool() : false;
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

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        painter->save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(m.cardRect, 6, 6);
        painter->setClipPath(clipPath);

        painter->setPen(Qt::NoPen);

        bool isWaitingThumb = false;
        if (m_pathRole != -1 && thumb.isNull()) {
            QString path = index.data(m_pathRole).toString();
            QString ext = QFileInfo(path).suffix().toLower();
            if (UiHelper::isGraphicsFile(ext) || ext == "svg") {
                isWaitingThumb = true;
            }
        }

        painter->setBrush(isWaitingThumb ? QColor("#3A3A3A") : QColor("#2d2d2d"));
        painter->drawRect(m.cardRect);

        if (hasThumb) {
            if (!thumb.isNull()) {
                QPixmap scaled = thumb.scaled(m.cardRect.size(),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
                int x = m.cardRect.center().x() - scaled.width() / 2;
                int y = m.cardRect.center().y() - scaled.height() / 2;
                painter->drawPixmap(x, y, scaled);
            }
        } else {
            QIcon icon = qvariant_cast<QIcon>(decoData);
            if (!icon.isNull()) {
                int iconSize = qMin(m.cardRect.width(), m.cardRect.height()) * 0.6;
                QRect iconRect(m.cardRect.center().x() - iconSize / 2,
                               m.cardRect.center().y() - iconSize / 2,
                               iconSize, iconSize);
                icon.paint(painter, iconRect);
            }
        }
        painter->restore();

        painter->save();
        if (isSelected) {
            painter->setPen(QPen(QColor("#3498db"), 3));
        } else {
            painter->setPen(QPen(QColor("#4a4a4a"), 1));
        }
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m.cardRect, 6, 6);
        painter->restore();

        if (m_pinnedRole != -1 && m_managedRole != -1) {
            bool isPinned = index.data(m_pinnedRole).toBool();
            bool isManaged = index.data(m_managedRole).toBool();
            bool isDir = (m_typeRole != -1) ? index.data(m_typeRole).toString() == "folder" : false;
            double progress = (m_registrationProgressRole != -1) ? index.data(m_registrationProgressRole).toDouble() : -1.0;

            QRect statusRect(m.cardRect.right() - 22, m.cardRect.top() + 8, 16, 16);
            if (isPinned) {
                UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16).paint(painter, statusRect);
            } else if (isDir && progress >= 0.0 && progress < 1.0) {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(QColor(60, 60, 60, 180), 2));
                painter->drawEllipse(statusRect.adjusted(1, 1, -1, -1));
                QPen pPen(QColor("#3498db"), 2);
                pPen.setCapStyle(Qt::RoundCap);
                painter->setPen(pPen);
                int spanAngle = -qRound(progress * 360 * 16);
                painter->drawArc(statusRect.adjusted(1, 1, -1, -1), 90 * 16, spanAngle);
                painter->restore();
            } else if (isManaged || (isDir && progress >= 1.0)) {
                UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, statusRect);
            }
        }

        if (m_pathRole != -1) {
            QString type = (m_typeRole != -1) ? index.data(m_typeRole).toString() : "";
            QString path = index.data(m_pathRole).toString();
            QFileInfo info(path);
            QString ext;
            if (type == "category" || type == "folder") {
                ext = "DIR";
            } else {
                ext = info.isDir() ? "DIR" : info.suffix().toUpper();
            }
            if (ext.isEmpty()) ext = "FILE";
            QColor badgeColor = UiHelper::getExtensionColor(ext);

            if (!hasThumb) {
                badgeColor.setAlpha(160);
            }

            QRect extRect(m.cardRect.left() + 8, m.cardRect.top() + 8, 36, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeColor);
            painter->drawRoundedRect(extRect, 2, 2);
            painter->setPen(hasThumb ? QColor("#FFFFFF") : QColor(255, 255, 255, 180));
            QFont extFont = painter->font(); extFont.setPointSize(8); extFont.setBold(true);
            painter->setFont(extFont);
            painter->drawText(extRect, Qt::AlignCenter, ext);
        }

        if (m_ratingRole != -1) {
            int rating = index.data(m_ratingRole).toInt();
            QString colorStr = (m_colorRole != -1) ? index.data(m_colorRole).toString() : "";

            if (!colorStr.isEmpty()) {
                QColor bgColor = UiHelper::parseColorName(colorStr);
                if (bgColor.isValid()) {
                    painter->save();
                    painter->setBrush(bgColor);
                    painter->setPen(Qt::NoPen);
                    QRect totalRect = m.banRect.united(m.starRect(4));
                    painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
                    painter->restore();
                }
            }

            bool shouldShowRating = (rating > 0) || isSelected;
            if (shouldShowRating) {
                QColor bgColor = colorStr.isEmpty() ? QColor(0,0,0,0) : UiHelper::parseColorName(colorStr);

                double luminance = 0.0;
                if (bgColor.isValid() && bgColor.alpha() > 0) {
                    luminance = (0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue()) / 255.0;
                }

                QColor starColor, emptyStarColor;
                if (colorStr.isEmpty()) {
                    starColor      = QColor("#CCCCCC");
                    emptyStarColor = QColor("#888888");
                } else if (luminance < 0.5) {
                    starColor      = QColor("#FFFFFF");
                    emptyStarColor = QColor(255, 255, 255, 160);
                } else {
                    starColor      = QColor("#1A1A1A");
                    emptyStarColor = QColor(0, 0, 0, 140);
                }

                UiHelper::getIcon("no_color", starColor, m.banRect.width()).paint(painter, m.banRect);
                QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(m.starSize, m.starSize), starColor);
                QPixmap emptyStar = UiHelper::getPixmap("star", QSize(m.starSize, m.starSize), emptyStarColor);
                for (int i = 0; i < 5; ++i) {
                    painter->drawPixmap(m.starRect(i), (i < rating) ? filledStar : emptyStar);
                }
            }
        }

        painter->save();
        QString name = index.data(Qt::DisplayRole).toString();
        painter->setPen(isSelected ? QColor("#3498db") : QColor("#EEEEEE"));

        if (m_managedRole != -1 && !isSelected && !index.data(m_managedRole).toBool()) {
            painter->setPen(QColor(238, 238, 238, 120));
        }

        QFont textFont = painter->font();
        textFont.setPointSize(8);
        painter->setFont(textFont);

        QString displayName = name;
        displayName.replace("_", "_\u200B");
        displayName.replace(".", ".\u200B");

        QTextLayout textLayout(displayName, painter->font());
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textOption.setAlignment(Qt::AlignCenter);
        textLayout.setTextOption(textOption);

        textLayout.beginLayout();
        int lineCount = 0;
        int textWidth = m.textRect.width() - 8;
        int currentY = m.textRect.top();
        int fontHeight = option.fontMetrics.height();

        struct RenderLine {
            QString text;
            int y;
        };
        QList<RenderLine> linesToRender;

        while (true) {
            QTextLine line = textLayout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(textWidth);
            lineCount++;

            if (lineCount == 1) {
                int start = line.textStart();
                int len = line.textLength();
                linesToRender.append({displayName.mid(start, len), currentY});
                currentY += fontHeight;
            } else if (lineCount == 2) {
                QTextLine nextLine = textLayout.createLine();
                if (nextLine.isValid()) {
                    int start = line.textStart();
                    QString remainingText = displayName.mid(start);
                    QString elidedRemaining = option.fontMetrics.elidedText(remainingText, Qt::ElideMiddle, textWidth);
                    linesToRender.append({elidedRemaining, currentY});
                } else {
                    int start = line.textStart();
                    int len = line.textLength();
                    linesToRender.append({displayName.mid(start, len), currentY});
                }
                break;
            }
        }
        textLayout.endLayout();

        for (const auto& rLine : linesToRender) {
            QRect lineRect(m.textRect.left() + 4, rLine.y, textWidth, fontHeight);
            painter->drawText(lineRect, Qt::AlignCenter, rLine.text);
        }
        painter->restore();

        if (!isSelected && m_isEmptyRole != -1 && m_typeRole != -1) {
            if (index.data(m_typeRole).toString() == "folder" && index.data(m_isEmptyRole).toBool()) {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(QColor("#41F2F2"), 1, Qt::DashLine));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(m.cardRect, 6, 6);
                painter->restore();
            }
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return QStyledItemDelegate::sizeHint(option, index);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            editor->setStyleSheet(
                "background-color: #2D2D2D; color: white; selection-background-color: #3498db; "
                "border: 1px solid #3498db; border-radius: 4px; padding: 0 4px;"
            );
        }
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        Metrics m = calculateMetrics(option);
        editor->setGeometry(m.textRect.adjusted(1, 5, -1, -5));
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (lineEdit) {
            lineEdit->setText(value);

            bool isFolder = (m_typeRole != -1) ? (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category") : false;

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

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;
        QString value = lineEdit->text();
        if (value.isEmpty() || value == index.data(Qt::DisplayRole).toString()) return;

        if (model->setData(index, value, Qt::EditRole)) {
            QAbstractItemView* view = qobject_cast<QAbstractItemView*>(editor->parentWidget()->parentWidget());
            if (view) {
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

    bool eventFilter(QObject* obj, QEvent* event) override {
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

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override {
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

private:
    int m_hasThumbnailRole = -1;
    int m_ratingRole = -1;
    int m_pathRole = -1;
    int m_pinnedRole = -1;
    int m_managedRole = -1;
    int m_typeRole = -1;
    int m_isEmptyRole = -1;
    int m_colorRole = -1;
    int m_registrationProgressRole = -1;
};

} // namespace

JustifiedResultView::JustifiedResultView(QWidget* parent) 
    : IScanResultView(parent) {
    m_view = new DropJustifiedView(parent);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    
    if (auto* lv = qobject_cast<QListView*>(m_view)) lv->setSelectionRectVisible(true);

    QPalette p = m_view->palette();
    p.setColor(QPalette::Highlight, QColor(55, 138, 221, 80)); 
    p.setColor(QPalette::HighlightedText, Qt::white);
    m_view->setPalette(p);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu); 
 
    m_view->setDragEnabled(true); 
    m_view->setAcceptDrops(true);
    m_view->setDragDropMode(QAbstractItemView::DragDrop); 
 
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed); 

    m_view->setAspectRatioRole(AspectRatioRole);
    auto* delegate = new JustifiedThumbnailDelegate(this);
    delegate->setHasThumbnailRole(HasThumbnailRole);
    delegate->setRatingRole(RatingRole);
    delegate->setPathRole(PathRole);
    delegate->setPinnedRole(PinnedRole);
    delegate->setManagedRole(ManagedRole);
    delegate->setTypeRole(TypeRole);
    delegate->setIsEmptyRole(IsEmptyRole);
    delegate->setColorRole(ColorRole);
    delegate->setRegistrationProgressRole(RegistrationProgressRole);
    m_view->setItemDelegate(delegate);

    m_view->setStyleSheet( 
        "QAbstractItemView { background-color: transparent; border: none; outline: none; }" 
        "QAbstractItemView::item { background: transparent; }" 
        "QAbstractItemView::item:selected { background-color: transparent; }" 
        "QAbstractItemView::item:hover { background-color: transparent; }"
    );
}

JustifiedResultView::~JustifiedResultView() {
}

QWidget* JustifiedResultView::getWidget() {
    return m_view;
}

QAbstractItemView* JustifiedResultView::getBaseView() {
    return m_view;
}

void JustifiedResultView::setModel(QAbstractItemModel* model) {
    m_view->setModel(model);
}

void JustifiedResultView::setIconSize(int size) {
    m_view->setTargetRowHeight(size);
}

void JustifiedResultView::refreshLayout() {
    m_view->setLayoutMode(JustifiedView::JustifiedMode);
    m_view->doItemsLayout();
}

} // namespace ArcMeta
