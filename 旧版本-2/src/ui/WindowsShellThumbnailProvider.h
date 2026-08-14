#pragma once

#include <QObject>
#include <QIcon>
#include <QString>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QMap>

namespace ArcMeta {

class IconLoadNotifier : public QObject {
    Q_OBJECT
signals:
    void iconLoaded();
public:
    static IconLoadNotifier& instance() {
        static IconLoadNotifier inst;
        return inst;
    }
private:
    IconLoadNotifier(QObject* parent = nullptr) : QObject(parent) {}
};

class WindowsShellThumbnailProvider : public QObject {
    Q_OBJECT
public:
    static WindowsShellThumbnailProvider& instance();

    static QIcon getFileIcon(const QString& filePath, int size = 18);
    static QImage getShellThumbnail(const QString& path, int size);

signals:
    void requestIconLoad(const QString& filePath, const QString& key, bool isDir, bool isRoot);

private slots:
    void handleIconLoad(const QString& filePath, const QString& key, bool isDir, bool isRoot);

private:
    WindowsShellThumbnailProvider();
    ~WindowsShellThumbnailProvider() override = default;

    static QMutex& fileIconMutex();
    static QMap<QString, QIcon>& fileIconCache();
    static QMutex& loadingMutex();
    static QSet<QString>& loadingKeys();
};

} // namespace ArcMeta
