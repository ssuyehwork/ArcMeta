#include "ListResultView.h"
#include "DropTreeView.h"
#include "TreeItemDelegate.h"
#include "ContentPanel.h"
#include "../core/AppConfig.h"
#include "../core/ModelContract.h"
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QLineEdit>
#include <QTimer>

namespace ArcMeta {

namespace {

class ListThumbnailDelegate : public QStyledItemDelegate {
public:
    explicit ListThumbnailDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        bool isSelected = (option.state & QStyle::State_Selected);
        bool isHovered = (option.state & QStyle::State_MouseOver);

        if (isSelected || isHovered) {
            painter->save();
            QColor bg = isSelected ? QColor("#378ADD") : QColor("#2a2d2e");
            if (isSelected) bg.setAlphaF(0.15f);
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRect(option.rect);
            painter->restore();
        }

        int padding = 3;
        int side = option.rect.height() - (padding * 2);
        if (side <= 0) side = 16;

        QRect squareRect(option.rect.left() + 6, option.rect.top() + padding, side, side);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#2d2d2d"));
        QPainterPath cardPath;
        cardPath.addRoundedRect(squareRect, 4, 4);
        painter->drawPath(cardPath);

        QVariant decoData = index.data(Qt::DecorationRole);

        if (decoData.canConvert<QPixmap>()) {
            QPixmap thumb = decoData.value<QPixmap>();
            if (!thumb.isNull()) {
                QPixmap scaled = thumb.scaled(squareRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                int x = squareRect.center().x() - scaled.width() / 2;
                int y = squareRect.center().y() - scaled.height() / 2;
                painter->drawPixmap(x, y, scaled);
            }
        } else {
            QIcon icon = qvariant_cast<QIcon>(decoData);
            if (!icon.isNull()) {
                int iconSize = side * 0.6;
                QRect iconRect(squareRect.center().x() - iconSize / 2,
                               squareRect.center().y() - iconSize / 2,
                               iconSize, iconSize);
                icon.paint(painter, iconRect);
            }
        }

        QString name = index.data(Qt::DisplayRole).toString();
        QColor textColor = isSelected ? QColor("#FFFFFF") : QColor("#EEEEEE");

        bool isManaged = index.data(ManagedRole).toBool();
        if (!isManaged && !isSelected) {
            textColor = QColor(238, 238, 238, 120);
        }

        painter->setPen(textColor);
        painter->setFont(option.font);

        QRect textRect = option.rect;
        textRect.setLeft(squareRect.right() + 10);

        QString elidedText = option.fontMetrics.elidedText(name, Qt::ElideMiddle, textRect.width() - 10);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

        painter->restore();
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            editor->setStyleSheet(
                "background-color: #2D2D2D; color: #FFFFFF; "
                "selection-background-color: #378ADD; "
                "border: 1px solid #378ADD; border-radius: 6px; padding: 2px;"
            );
        }
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;

        lineEdit->setText(value);

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

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        int padding = 3;
        int side = option.rect.height() - (padding * 2);
        if (side <= 0) side = 16;
        int textLeft = option.rect.left() + 6 + side + 10;
        QRect editorRect = option.rect;
        editorRect.setLeft(textLeft);
        editor->setGeometry(editorRect);
    }
};

} // namespace

ListResultView::ListResultView(QWidget* parent) 
    : IScanResultView(parent) {
    m_treeView = new DropTreeView(parent);
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_treeView->setSortingEnabled(true); 
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu); 
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    
    QPalette tp = m_treeView->palette();
    tp.setColor(QPalette::Highlight, QColor(55, 138, 221, 80));
    tp.setColor(QPalette::HighlightedText, Qt::white);
    m_treeView->setPalette(tp);
     
    m_treeView->setDragEnabled(true); 
    m_treeView->setAcceptDrops(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop); 
 
    m_treeView->setExpandsOnDoubleClick(false); 
    m_treeView->setRootIsDecorated(false); 
     
    m_treeView->setItemDelegate(new TreeItemDelegate(this)); 
    m_treeView->setItemDelegateForColumn(0, new ListThumbnailDelegate(m_treeView));
 
    m_treeView->setStyleSheet( 
        "QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }" 
        "QTreeView::item { height: 28px; color: #EEEEEE; padding-left: 0px; }" 
        "QTreeView::item:selected { background-color: rgba(52, 152, 219, 0.2); border-left: 2px solid #3498db; }"
        "QTreeView::item:hover { background-color: #2A2A2A; }"
        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }" 
    ); 
 
    m_treeView->header()->setDefaultAlignment(Qt::AlignCenter);
    m_treeView->header()->setStyleSheet( 
        "QHeaderView::section { background-color: #252525; color: #B0B0B0; border: none; border-right: 1px solid #333333; height: 32px; font-size: 11px; }" 
    ); 
    
    auto* header = m_treeView->header();
    header->setStretchLastSection(false);
    header->setCascadingSectionResizes(false);
    header->setMinimumSectionSize(30);
}

ListResultView::~ListResultView() {
}

QWidget* ListResultView::getWidget() {
    return m_treeView;
}

QAbstractItemView* ListResultView::getBaseView() {
    return m_treeView;
}

void ListResultView::setModel(QAbstractItemModel* model) {
    m_treeView->setModel(model);

    auto* header = m_treeView->header();
    header->setStretchLastSection(false);
    header->setCascadingSectionResizes(false);
    header->setMinimumSectionSize(30);

    QByteArray headerState = AppConfig::instance().getValue("UI/ListHeaderState").toByteArray();
    if (!headerState.isEmpty()) {
        header->restoreState(headerState);
    } 

    for(int i = 0; i <= 7; ++i) header->setSectionHidden(i, false);

    header->resizeSection(0, 400); // 名称
    header->resizeSection(1, 40);  // 状态
    header->resizeSection(2, 60);  // 星级
    header->resizeSection(3, 60);  // 颜色标记
    header->resizeSection(4, 100); // 标签
    header->resizeSection(5, 80);  // 类型
    header->resizeSection(6, 80);  // 大小
    header->resizeSection(7, 150); // 修改日期

    for(int i = 1; i <= 7; ++i) {
        header->setSectionResizeMode(i, QHeaderView::Interactive);
    }
    header->setSectionResizeMode(0, QHeaderView::Stretch);

    disconnect(header, &QHeaderView::sectionResized, nullptr, nullptr);
    connect(header, &QHeaderView::sectionResized, this, [this, header](int index, int oldSize, int newSize) {
        Q_UNUSED(oldSize);
        static bool guard = false; 
        if (guard || index == 0) return; 
        
        guard = true;
        
        if (index == 7 && newSize < 150) {
            header->resizeSection(7, 150);
            guard = false;
            return;
        }

        int currentTotal = header->length();
        int maxAvailable = m_treeView->viewport()->width();
        
        if (currentTotal > maxAvailable && maxAvailable > 100) {
             int allowed = newSize - (currentTotal - maxAvailable);
             int minAllowed = header->minimumSectionSize();
             if (index == 7) minAllowed = 150;
             
             header->resizeSection(index, qMax(minAllowed, allowed));
        }
        
        AppConfig::instance().setValue("UI/ListHeaderState", header->saveState());
        
        guard = false;
    });
}

void ListResultView::setIconSize(int size) {
    m_treeView->setIconSize(QSize(size - 8, size - 8));
    
    static int lastTreeHeight = -1;
    if (lastTreeHeight != size) {
        m_treeView->setStyleSheet( 
            QString("QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }" 
                    "QTreeView::item { height: %1px; color: #EEEEEE; padding-left: 0px; }" 
                    "QTreeView::item:selected { background-color: rgba(52, 152, 219, 0.2); border-left: 2px solid #3498db; }"
                    "QTreeView::item:hover { background-color: #2A2A2A; }"
                    "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }")
            .arg(size)
        );
        lastTreeHeight = size;
    }
}

void ListResultView::refreshLayout() {
}

} // namespace ArcMeta
