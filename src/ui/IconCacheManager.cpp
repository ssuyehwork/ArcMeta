#include "IconCacheManager.h"
#include <QFileIconProvider>
#include <QFileInfo>

namespace ArcMeta {

IconCacheManager& IconCacheManager::instance() {
    static IconCacheManager inst;
    return inst;
}

IconCacheManager::IconCacheManager(QObject* parent)
    : QObject(parent) {
}

QIcon IconCacheManager::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    {
        QReadLocker lock(&m_cacheLock);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end()) return *it;
    }

    QFileIconProvider provider;
    QIcon icon;
    if (isDir) {
        icon = provider.icon(QFileIconProvider::Folder);
    } else {
        if (key.length() > 12) key = "unknown";
        icon = provider.icon(QFileInfo("dummy." + key));
        if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
    }

    {
        QWriteLocker lock(&m_cacheLock);
        m_iconCache[key] = icon;
    }
    return icon;
}

} // namespace ArcMeta
