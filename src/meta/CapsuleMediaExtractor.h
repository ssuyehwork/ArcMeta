#ifndef CAPSULEMEDIAEXTRACTOR_H
#define CAPSULEMEDIAEXTRACTOR_H

#include <QImage>
#include <QString>

namespace ArcMeta {

class CapsuleMediaExtractor {
public:
    // 受控库模式专属：提取并保存至 .arc 胶囊内部 [baseName]_thumbnail.png
    static QImage getCapsuleThumbnail(const QString& mainAssetPath, int size = 512);
};

} // namespace ArcMeta

#endif // CAPSULEMEDIAEXTRACTOR_H
