#include "CapsuleMediaExtractor.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>

namespace ArcMeta {

QImage CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(const QString& mainAssetPath) {
    QFileInfo fi(mainAssetPath);
    QString thumbPath = fi.absolutePath() + "/" + fi.completeBaseName() + "_thumbnail.png";
    if (QFile::exists(thumbPath)) {
        QImage img;
        if (img.load(thumbPath)) return img;
    }
    return QImage(); // 绝不实时提取
}

QImage CapsuleMediaExtractor::getCapsuleThumbnail(const QString& mainAssetPath, int size) {
    QFileInfo fi(mainAssetPath);
    QString containerDir = fi.absolutePath();
    QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";

    // 1. 优先查 .arc 胶囊内部
    if (QFile::exists(thumbPath)) {
        QImage arcThumb;
        if (arcThumb.load(thumbPath)) return arcThumb;
    }

    // 2. 提取图像
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(mainAssetPath);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
        }
    } else if (ext == "psd" || ext == "psb") {
        img = MediaColorExtractor::extractEmbeddedPsdThumbnail(mainAssetPath);
    } else if (ext == "ai") {
        img = MediaColorExtractor::extractEmbeddedAiPreview(mainAssetPath, size);
    } else if (ext == "eps") {
        img = MediaColorExtractor::extractEmbeddedEpsPreview(mainAssetPath, size);
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(mainAssetPath, size);
        if (img.isNull()) img.load(mainAssetPath);
    }

    // 3. 100% 仅落盘保存至 .arc 胶囊容器内部！
    if (!img.isNull() && containerDir.endsWith(".arc", Qt::CaseInsensitive)) {
        img.save(thumbPath, "PNG");
    }
    return img;
}

} // namespace ArcMeta
