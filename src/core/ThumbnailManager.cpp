#include "ThumbnailManager.h"
#include "../ui/UiHelper.h"
#include <QtConcurrent>
#include <QDir>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>

namespace ArcMeta {

ThumbnailManager& ThumbnailManager::instance() {
    static ThumbnailManager inst;
    return inst;
}

ThumbnailManager::ThumbnailManager(QObject* parent) : QObject(parent) {
    m_memoryCache.setMaxCost(500); // 缓存 500 张
    m_diskCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/Thumbnails";
    QDir().mkpath(m_diskCacheDir);
}

ThumbnailManager::~ThumbnailManager() {}

QPixmap ThumbnailManager::getThumbnail(const QString& path, int size, QObject* receiver, std::function<void(const QPixmap&)> callback) {
    QString key = getCacheKey(path, size);

    {
        QMutexLocker locker(&m_mutex);
        if (m_memoryCache.contains(key)) {
            return *m_memoryCache.object(key);
        }
        if (m_pendingRequests.contains(key)) return QPixmap();
        m_pendingRequests.insert(key);
    }

    // 尝试从磁盘加载
    QString diskPath = getDiskCachePath(key);
    if (QFile::exists(diskPath)) {
        QPixmap pix(diskPath);
        if (!pix.isNull()) {
            QMutexLocker locker(&m_mutex);
            m_memoryCache.insert(key, new QPixmap(pix));
            return pix;
        }
    }

    // 异步拉取
    QPointer<QObject> weakReceiver(receiver);
    QtConcurrent::run([this, path, size, key, diskPath, weakReceiver, callback]() {
        QPixmap thumb = UiHelper::getShellThumbnail(path, size);
        if (!thumb.isNull()) {
            thumb.save(diskPath, "PNG");
        }

        QMetaObject::invokeMethod(this, [this, key, thumb, weakReceiver, callback]() {
            {
                QMutexLocker locker(&m_mutex);
                if (!thumb.isNull()) {
                    m_memoryCache.insert(key, new QPixmap(thumb));
                }
                m_pendingRequests.remove(key);
            }
            if (!thumb.isNull() && weakReceiver && callback) callback(thumb);
        });
    });

    return QPixmap();
}

void ThumbnailManager::clearMemoryCache() {
    QMutexLocker locker(&m_mutex);
    m_memoryCache.clear();
}

QString ThumbnailManager::getCacheKey(const QString& path, int size) {
    QByteArray data = path.toUtf8() + QByteArray::number(size);
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex();
}

QString ThumbnailManager::getDiskCachePath(const QString& key) {
    return m_diskCacheDir + "/" + key + ".png";
}

} // namespace ArcMeta
