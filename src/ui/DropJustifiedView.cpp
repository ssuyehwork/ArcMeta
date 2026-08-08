#include "DropJustifiedView.h"
#include "ContentPanel.h"
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPixmap>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QFileInfo>

namespace ArcMeta {

DropJustifiedView::DropJustifiedView(QWidget* parent) : JustifiedView(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
}

#include "DragPayloadFactory.h"

void DropJustifiedView::dragEnterEvent(QDragEnterEvent* event) {
    if (DragPayloadFactory::hasLocalUriFormat(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        JustifiedView::dragEnterEvent(event);
    }
}

void DropJustifiedView::dragMoveEvent(QDragMoveEvent* event) {
    if (DragPayloadFactory::hasLocalUriFormat(event->mimeData())) {
        JustifiedView::dragMoveEvent(event);
        event->acceptProposedAction();
    } else {
        JustifiedView::dragMoveEvent(event);
    }
}

void DropJustifiedView::dropEvent(QDropEvent* event) {
    if (DragPayloadFactory::hasLocalUriFormat(event->mimeData())) {
        QStringList paths = DragPayloadFactory::extractPathsFromMime(event->mimeData());
        QModelIndex idx = indexAt(event->position().toPoint());
        if (!paths.isEmpty()) {
            emit pathsDropped(paths, idx);
        }
        event->acceptProposedAction();
    } else {
        JustifiedView::dropEvent(event);
    }
}

void DropJustifiedView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    QMimeData* mimeData = DragPayloadFactory::createMimeDataFromIndexes(indexes);

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    
    QPixmap pix(1, 1);
    pix.fill(Qt::transparent);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(0, 0));
    
    drag->exec(supportedActions, Qt::MoveAction);
}

} // namespace ArcMeta
