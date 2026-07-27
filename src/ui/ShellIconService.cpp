#include "ShellIconService.h"
#include <QFileIconProvider>
#include <QFileInfo>

namespace ArcMeta {

ShellIconService& ShellIconService::instance() {
    static ShellIconService inst;
    return inst;
}

ShellIconService::ShellIconService(QObject* parent) : QObject(parent) {
}

ShellIconService::~ShellIconService() {
    QWriteLocker lock(&m_iconCacheLock);
    m_icon_cache.clear();
}

QIcon ShellIconService::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    {
        QReadLocker lock(&m_iconCacheLock);
        auto it = m_icon_cache.find(key);
        if (it != m_icon_cache.end()) return *it;
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
        QWriteLocker lock(&m_iconCacheLock);
        m_icon_cache[key] = icon;
    }
    return icon;
}

} // namespace ArcMeta
