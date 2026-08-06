#ifndef CAPSULEMEDIAEXTRACTOR_H
#define CAPSULEMEDIAEXTRACTOR_H

#include <QImage>
#include <QString>

namespace ArcMeta {

class CapsuleMediaExtractor {
public:
    // 🚨 UI 热路径专属：只读取磁盘上已存在的 _thumbnail.png，绝不做任何实时提取，
    // 保证毫秒级返回，杜绝 LibraryAssetModel::loadThumbnailsForRows 间接触发 Ghostscript
    static QImage getCapsuleThumbnailReadOnly(const QString& mainAssetPath);

    // 后台管道 MediaExtractorPipeline 专属：查不到就执行完整的实时提取
    // （含 Ghostscript / Shell / 内嵌解析），并落盘缓存供后续读取
    static QImage getCapsuleThumbnail(const QString& mainAssetPath, int size = 512);
};

} // namespace ArcMeta

#endif // CAPSULEMEDIAEXTRACTOR_H
