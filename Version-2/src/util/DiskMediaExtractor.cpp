#include "DiskMediaExtractor.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QSvgRenderer>
#include <QPainter>

namespace ArcMeta {

QString DiskMediaExtractor::diskThumbCachePath(const QString& path, int size) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir(appDir).filePath(".arcmeta/disk_thumbs/");
    QDir().mkpath(cacheDir);

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    return cacheDir + QString::number(qHash(hashKey), 16) + ".png";
}

QImage DiskMediaExtractor::getDiskThumbnail(const QString& path, int size) {
    // 1. 查 disk_thumbs 缓存
    QString cachePath = diskThumbCachePath(path, size);
    if (QFile::exists(cachePath)) {
        QImage cached;
        if (cached.load(cachePath)) return cached;
    }

    // 2. 提取图像
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(path);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
        }
    } else if (ext == "psd" || ext == "psb") {
        img = MediaColorExtractor::extractEmbeddedPsdThumbnail(path);
    } else if (ext == "ai") {
        img = MediaColorExtractor::extractEmbeddedAiPreview(path, size);
    } else if (ext == "eps") {
        img = MediaColorExtractor::extractEmbeddedEpsPreview(path, size);
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    // 3. 100% 仅落盘存入 disk_thumbs/ 目录
    if (!img.isNull()) {
        img.save(cachePath, "PNG");
    }
    return img;
}

} // namespace ArcMeta
