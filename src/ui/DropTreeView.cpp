#include "DropTreeView.h"
#include "CategoryModel.h"
#include "ContentPanel.h"
#include <QDrag>
#include <QPixmap>
#include <QAbstractProxyModel>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QStringList>
#include <QFileInfo>
#include "Logger.h"

namespace ArcMeta {

DropTreeView::DropTreeView(QWidget* parent) : QTreeView(parent) {
    setAcceptDrops(true);
    setDropIndicatorShown(true);
}

void DropTreeView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTreeView::dragEnterEvent(event);
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    // 2026-06-xx 按照用户要求：支持内部拖拽重排与外部拖拽入库共存
    // 逻辑：如果是从本视图发起的拖拽，通常是内部重排逻辑
    if (event->mimeData()->hasUrls() && event->source() != this) {
        QModelIndex idx = indexAt(event->position().toPoint());
        if (idx.isValid()) setCurrentIndex(idx);
        event->acceptProposedAction();
    } else {
        QTreeView::dragMoveEvent(event);
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    // 2026-06-xx 物理纠偏：只有当拖拽源不是本视图时，才判定为“归类/导入”
    // 理由：防止在侧边栏内部调整分类顺序时意外触发文件导入逻辑
    if (event->mimeData()->hasUrls() && event->source() != this) {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths << QDir::toNativeSeparators(url.toLocalFile());
            }
        }
        QModelIndex idx = indexAt(event->position().toPoint());
        if (!paths.isEmpty()) {
            emit pathsDropped(paths, idx);
        }
        event->acceptProposedAction();
    } else {
        // 内部拖拽：转发给系统，触发 model->dropMimeData 处理重排，并通过 rowsMoved 信号同步 DB
        QTreeView::dropEvent(event);
    }
}

void DropTreeView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    Logger::log(QString("[树形视图] 开始拖拽 | 选中项数量: %1").arg(indexes.count()));

    // 核心增强：拦截并注入物理路径 QUrl，确保 CategoryPanel 接收校验通过
    QMimeData* mimeData = model()->mimeData(indexes);
    QList<QUrl> urls;
    for (const QModelIndex& idx : indexes) {
        if (idx.column() != 0) continue;
        
        // 2026-03-xx 物理还原：兼容性提取逻辑
        // NavPanel 使用 UserRole+1 (硬编码)，ContentPanel 使用 PathRole (枚举)
        QString path = idx.data(Qt::UserRole + 1).toString(); 
        
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            path = idx.data(PathRole).toString(); 
        }
        
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            urls << QUrl::fromLocalFile(path);
        }
    }
    
    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    
    // 物理还原：消除卡片快照干扰，使用 1x1 透明像素
    QPixmap pix(1, 1);
    pix.fill(Qt::transparent);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(0, 0));
    
    drag->exec(supportedActions, Qt::MoveAction);
}

void DropTreeView::keyboardSearch(const QString& search) {
    Q_UNUSED(search);
}

} // namespace ArcMeta
