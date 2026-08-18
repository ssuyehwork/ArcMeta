#pragma once

#include <QObject>
#include <QIcon>
#include <QHash>
#include <QString>
#include <QReadWriteLock>

namespace QuarkMeta {

class IconCacheManager : public QObject {
    Q_OBJECT
public:
    static IconCacheManager& instance();

    QIcon getCachedIcon(const QString& ext, bool isDir);

private:
    explicit IconCacheManager(QObject* parent = nullptr);

    mutable QReadWriteLock m_cacheLock;
    QHash<QString, QIcon> m_iconCache;
};

} // namespace QuarkMeta
