#include "CapsuleMediaExtractor.h"
#include <QFileInfo>
#include <QFile>

namespace ArcMeta {

QImage CapsuleMediaExtractor::getCapsuleThumbnail(const QString& mainAssetPath, int size) {
    Q_UNUSED(size);
    QFileInfo fi(mainAssetPath);
    QString thumbPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_thumbnail.png";

    // 仅检查 .arc 胶囊内是否有现成的 _thumbnail.png
    if (QFile::exists(thumbPath)) {
        QImage img;
        if (img.load(thumbPath)) return img;
    }

    // 没有现成图片直接返回空，绝不上锁、绝不实时提取！
    return QImage();
}

} // namespace ArcMeta
