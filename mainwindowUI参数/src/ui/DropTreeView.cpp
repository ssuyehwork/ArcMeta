#include "DropTreeView.h"
#include "../models/CategoryModel.h"
#include <QDrag>
#include <QPixmap>
/* [MODIFIED] 2026-03-11 必须包含此头文件以支持代理模型穿透判定 */
#include <QAbstractProxyModel>

DropTreeView::DropTreeView(QWidget* parent) : QTreeView(parent) {
    setAcceptDrops(true);
    setDropIndicatorShown(true);
}

void DropTreeView::dragEnterEvent(QDragEnterEvent* event) {
    /* [MODIFIED] 2026-03-11 核心修复：放行分类移动所需的默认 MIME 类型 */
    if (event->mimeData()->hasFormat("application/x-note-ids") || 
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
    } else {
        // 非内部数据显式 ignore，允许冒泡到 QuickWindow 处理外部导入
        event->ignore();
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    /* [MODIFIED] 2026-03-11 必须显式调用基类以显示原生拖拽指示线/目标高亮 */
    QTreeView::dragMoveEvent(event);

    if (event->mimeData()->hasFormat("application/x-note-ids") || 
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        QByteArray data = event->mimeData()->data("application/x-note-ids");
        QStringList idStrs = QString::fromUtf8(data).split(",", Qt::SkipEmptyParts);
        QList<int> ids;
        for (const QString& s : idStrs) ids << s.toInt();

        QModelIndex index = indexAt(event->position().toPoint());
        emit notesDropped(ids, index);
        event->acceptProposedAction();
    } else if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        /* [MODIFIED] 2026-03-11 允许原生分类排序事件流转至 Model 层 */
        QTreeView::dropEvent(event);
    } else {
        event->ignore();
    }
}

void DropTreeView::startDrag(Qt::DropActions supportedActions) {
    // 追踪拖拽 ID
    /* [MODIFIED] 2026-03-11 核心修复：支持代理模型穿透，确保拖拽时能正确设置 CategoryModel 的 draggingId */
    CategoryModel* catModel = qobject_cast<CategoryModel*>(model());
    if (!catModel) {
        if (auto* proxy = qobject_cast<QAbstractProxyModel*>(model())) {
            catModel = qobject_cast<CategoryModel*>(proxy->sourceModel());
        }
    }

    if (catModel && !selectedIndexes().isEmpty()) {
        catModel->setDraggingId(selectedIndexes().first().data(CategoryModel::IdRole).toInt());
    }

    // 禁用默认的快照卡片预览，改用 1x1 透明占位符
    QDrag* drag = new QDrag(this);
    drag->setMimeData(model()->mimeData(selectedIndexes()));
    
    QPixmap pix(1, 1);
    pix.fill(Qt::transparent);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(0, 0));
    
    drag->exec(supportedActions, Qt::MoveAction);
}

void DropTreeView::keyboardSearch(const QString& search) {
    // [MODIFIED] 2026-03-xx 物理级屏蔽键盘搜索：严禁在侧边栏获焦时由于输入字符导致的选择项漂移，
    // 这也是为了确保 Ctrl+Alt+S 等含有 S 键的组合快捷键不被 Qt 内部逻辑预截获。
    Q_UNUSED(search);
}
