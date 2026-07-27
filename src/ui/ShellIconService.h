#ifndef ARCMETA_SHELL_ICON_SERVICE_H
#define ARCMETA_SHELL_ICON_SERVICE_H

#include <QObject>
#include <QIcon>
#include <QHash>
#include <QReadWriteLock>
#include <QString>

namespace ArcMeta {

class ShellIconService : public QObject {
    Q_OBJECT
public:
    static ShellIconService& instance();

    QIcon getCachedIcon(const QString& ext, bool isDir);

private:
    ShellIconService(QObject* parent = nullptr);
    ~ShellIconService();

    QHash<QString, QIcon> m_icon_cache;
    mutable QReadWriteLock m_iconCacheLock;
};

} // namespace ArcMeta

#endif // ARCMETA_SHELL_ICON_SERVICE_H
