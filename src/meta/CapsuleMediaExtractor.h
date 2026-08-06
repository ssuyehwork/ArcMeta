#ifndef CAPSULEMEDIAEXTRACTOR_H
#define CAPSULEMEDIAEXTRACTOR_H

#include <QImage>
#include <QString>

namespace ArcMeta {

class CapsuleMediaExtractor {
public:
    // UI 热路径专属：只读已有缩略图（支持 .arc 胶囊内与 disk_thumbs 缓存）
    static QImage getCapsuleThumbnailReadOnly(const QString& mainAssetPath);

    // 后台管道提取与落盘缓存
    static QImage getCapsuleThumbnail(const QString& mainAssetPath, int size = 512);

    // 计算磁盘模式缩略图的哈希缓存路径
    static QString getDiskThumbCachePath(const QString& mainAssetPath);
};

} // namespace ArcMeta

#endif // CAPSULEMEDIAEXTRACTOR_H
