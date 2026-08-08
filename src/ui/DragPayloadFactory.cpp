#include "DragPayloadFactory.h"
#include "ContentPanel.h"
#include <QUrl>
#include <QDir>

namespace ArcMeta {

QMimeData* DragPayloadFactory::createMimeDataFromIndexes(const QModelIndexList& indexes) {
    QMimeData* mimeData = new QMimeData();
    QList<QUrl> urls;
    for (const QModelIndex& idx : indexes) {
        if (idx.column() != 0) continue;

        QString path;
        QVariant pathVar = idx.data(PathRole);
        if (pathVar.isValid()) {
            path = pathVar.toString();
        } else {
            path = idx.data(Qt::UserRole + 1).toString();
        }

        if (!path.isEmpty()) {
            urls << QUrl::fromLocalFile(path);
        }
    }

    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }
    return mimeData;
}

bool DragPayloadFactory::hasLocalUriFormat(const QMimeData* mimeData) {
    if (!mimeData) return false;
    return mimeData->hasFormat("text/uri-list");
}

QStringList DragPayloadFactory::extractPathsFromMime(const QMimeData* mimeData) {
    QStringList paths;
    if (!mimeData) return paths;
    if (mimeData->hasUrls()) {
        for (const QUrl& u : mimeData->urls()) {
            if (u.isLocalFile()) {
                paths << QDir::toNativeSeparators(u.toLocalFile());
            }
        }
    }
    return paths;
}

} // namespace ArcMeta
