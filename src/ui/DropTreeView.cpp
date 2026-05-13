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
    Logger::log(QString("[树形视图] 拖拽进入 | 格式: %1").arg(event->mimeData()->formats().join(",")));
    if (event->mimeData()->hasFormat("application/x-note-ids") || 
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist") ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        Logger::log("[树形视图] 已接受拖拽进入信号");
    } else {
        event->ignore();
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    QTreeView::dragMoveEvent(event);

    if (event->mimeData()->hasFormat("application/x-note-ids") || 
        event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist") ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    // 2026-06-xx 物理修复：校准坐标换算。
    // 在 Qt 6 中 event->position() 是窗口相对坐标，必须显式映射到 viewport 才能命中准确的 Item
    QModelIndex index = indexAt(viewport()->mapFrom(this, event->position().toPoint()));
    
    Logger::log(QString("[树形视图] 释放事件 | 原始坐标: (%1, %2) | 视口映射坐标: (%3, %4)")
                .arg(event->position().x()).arg(event->position().y())
                .arg(viewport()->mapFrom(this, event->position().toPoint()).x())
                .arg(viewport()->mapFrom(this, event->position().toPoint()).y()));

    Logger::log(QString("[树形视图] 释放事件 | 目标索引是否有效: %1 | 名称: %2")
                .arg(index.isValid() ? "是" : "否").arg(index.data().toString()));

    // 优先处理路径拖入 (收藏逻辑)
    if (event->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths << QDir::toNativeSeparators(url.toLocalFile());
            }
        }
        Logger::log(QString("[树形视图] 释放的文件路径: %1").arg(paths.join(",")));
        if (!paths.isEmpty()) {
            emit pathsDropped(paths, index);
            event->setDropAction(Qt::LinkAction); // 视觉上显示为“链接/快捷方式”
            event->accept();
            return;
        }
    }

    // 处理内部 ID 拖拽 (如果以后有用)
    if (event->mimeData()->hasFormat("application/x-note-ids")) {
        QByteArray byteData = event->mimeData()->data("application/x-note-ids");
        QStringList idStrs = QString::fromUtf8(byteData).split(",", Qt::SkipEmptyParts);
        QList<int> ids;
        for (const QString& s : idStrs) ids << s.toInt();

        emit notesDropped(ids, index);
        event->acceptProposedAction();
    } else if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        QTreeView::dropEvent(event);
    } else {
        event->ignore();
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
        Logger::log(QString("[树形视图] 正在尝试提取 Role+1 (导航面板) 对于 %1 : %2").arg(idx.data().toString()).arg(path));
        
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            path = idx.data(PathRole).toString(); 
            Logger::log(QString("[树形视图] 正在尝试提取 PathRole (内容面板) 对于 %1 : %2").arg(idx.data().toString()).arg(path));
        }
        
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            urls << QUrl::fromLocalFile(path);
        }
    }
    
    QStringList urlStrs;
    for(const QUrl& u : urls) urlStrs << u.toString();
    Logger::log(QString("[树形视图] 最终注入的物理路径列表: %1").arg(urlStrs.join(",")));

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
    
    Logger::log("[树形视图] 执行拖拽操作...");
    drag->exec(supportedActions, Qt::MoveAction);
}

void DropTreeView::keyboardSearch(const QString& search) {
    Q_UNUSED(search);
}

} // namespace ArcMeta
