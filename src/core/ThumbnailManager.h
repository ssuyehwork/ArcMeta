#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QCache>
#include <QMutex>

namespace ArcMeta {

/**
 * @brief 缩略图管理器
 * 实现缩略图的异步拉取与磁盘缓存，提升 I/O 效率并消除视觉闪烁。
 */
class ThumbnailManager : public QObject {
    Q_OBJECT
public:
    static ThumbnailManager& instance();

    /**
     * @brief 获取缩略图
     * @param path 文件物理路径
     * @param size 期望尺寸
     * @param receiver 回调接收者，用于安全检查
     * @param callback 获取成功后的回调
     * @return 如果缓存命中则立即返回 QPixmap，否则返回空并启动异步拉取
     */
    QPixmap getThumbnail(const QString& path, int size, QObject* receiver, std::function<void(const QPixmap&)> callback);

    /**
     * @brief 清除内存缓存，但不清除磁盘缓存
     */
    void clearMemoryCache();

private:
    ThumbnailManager(QObject* parent = nullptr);
    ~ThumbnailManager() override;

    QString getCacheKey(const QString& path, int size);
    QString getDiskCachePath(const QString& key);

    QCache<QString, QPixmap> m_memoryCache;
    QSet<QString> m_pendingRequests;
    QMutex m_mutex;
    QString m_diskCacheDir;
};

} // namespace ArcMeta
