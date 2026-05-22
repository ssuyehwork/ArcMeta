#include "DropTreeView.h"
#include "CategoryModel.h"
#include "ContentPanel.h"
#include "ToolTipOverlay.h"
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
        // 2026-06-xx 权限边界防御：检测拖拽源是否包含本地文件但路径为空 (通常由于 UAC 权限不匹配导致)
        bool hasInvalidUrl = false;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && url.toLocalFile().isEmpty()) {
                hasInvalidUrl = true;
                break;
            }
        }

        if (hasInvalidUrl) {
            ToolTipOverlay::instance()->showText(QCursor::pos(),
                "<b style='color:#e74c3c;'>[权限受限]</b> 无法跨权限拖拽，请尝试以管理员运行或使用导入按钮。",
                3000, QColor("#e74c3c"));
            event->ignore();
            return;
        }

        event->acceptProposedAction();
    } else {
        QTreeView::dragEnterEvent(event);
    }
}

void DropTreeView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        // 2026-06-xx 按照用户要求：实现拖拽过程中的目标项实时高亮
        QModelIndex idx = indexAt(event->position().toPoint());
        if (idx.isValid()) setCurrentIndex(idx);
        event->acceptProposedAction();
    } else {
        QTreeView::dragMoveEvent(event);
    }
}

void DropTreeView::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
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
