#ifndef DISKMEDIAEXTRACTOR_H
#define DISKMEDIAEXTRACTOR_H

#include <QImage>
#include <QString>

namespace QuarkMeta {

class DiskMediaExtractor {
public:
    // 磁盘模式专属：提取并保存至 .QuarkMeta/disk_thumbs/
    static QImage getDiskThumbnail(const QString& path, int size = 512);

private:
    static QString diskThumbCachePath(const QString& path, int size);
};

} // namespace QuarkMeta

#endif // DISKMEDIAEXTRACTOR_H
