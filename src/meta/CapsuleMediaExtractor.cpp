#include "CapsuleMediaExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace ArcMeta {

QImage CapsuleMediaExtractor::getCapsuleThumbnail(const QString& mainAssetPath, int size) {
    Q_UNUSED(size);
    QFileInfo fi(mainAssetPath);
    QString containerDir = fi.absolutePath();

    // 物理死规矩：只查 .arc 托管包里的 _thumbnail.png，绝不做任何多余的现场提取！
    QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";

    if (QFile::exists(thumbPath)) {
        QImage arcThumb;
        if (arcThumb.load(thumbPath)) {
            return arcThumb; // 找到了直接返回，0 毫秒延时！
        }
    }

    // 没找到直接返回空图片，交给前端画文件类型徽章，绝不拖泥带水！
    return QImage();
}

} // namespace ArcMeta
