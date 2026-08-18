#include "CleanListView.h"
#include <QDrag>
#include <QPixmap>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

CleanListView::CleanListView(QWidget* parent) : QListView(parent) {}

void CleanListView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    QMimeData* data = model()->mimeData(indexes);
    if (!data) return;

    QDrag* drag = new QDrag(this);
    drag->setMimeData(data);
    
    // 关键：在这里设置一个 1x1 像素的透明 Pixmap 用以消除臃肿的截图
    QPixmap transparentPixmap(1, 1);
    transparentPixmap.fill(Qt::transparent);
    drag->setPixmap(transparentPixmap);
    drag->setHotSpot(QPoint(0, 0));

    drag->exec(supportedActions, Qt::MoveAction);
}

void CleanListView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        event->acceptProposedAction();
    } else {
        // [MODIFIED] 非内部 ID 数据显式 ignore，允许冒泡到 QuickWindow 处理外部拖入
        event->ignore();
    }
}

void CleanListView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        event->acceptProposedAction();
    } else {
        // [MODIFIED] 非内部 ID 数据显式 ignore，允许冒泡到 QuickWindow 处理外部拖入
        event->ignore();
    }
}

void CleanListView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        QByteArray data = event->mimeData()->data("application/x-note-ids");
        QStringList idStrs = QString::fromUtf8(data).split(',', Qt::SkipEmptyParts);
        QList<int> ids;
        for (const QString& s : idStrs) ids << s.toInt();

        // 计算落点行号
        int row = -1;
        // [COMPAT] 适配 Qt6：使用 event->position().toPoint() 替换已废弃的 event->pos()
        QPoint dropPos = event->position().toPoint();
        QModelIndex index = indexAt(dropPos);
        if (index.isValid()) {
            row = index.row();
            // 如果落在项的下半部分，则视为移动到该项之后
            QRect rect = visualRect(index);
            if (dropPos.y() > rect.top() + rect.height() / 2) {
                row++;
            }
        } else {
            row = model()->rowCount();
        }

        if (!ids.isEmpty()) {
            emit internalMoveRequested(ids, row);
            event->acceptProposedAction();
        }
    } else {
        // [MODIFIED] 非内部 ID 数据显式 ignore，允许冒泡到 QuickWindow 处理外部拖入
        event->ignore();
    }
}
