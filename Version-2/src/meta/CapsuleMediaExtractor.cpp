#include "CapsuleMediaExtractor.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QCryptographicHash>
#include <QCoreApplication>

namespace ArcMeta {

std::mutex CapsuleMediaExtractor::s_qtGuiMutex;

QString CapsuleMediaExtractor::getDiskThumbCachePath(const QString& mainAssetPath) {
    if (mainAssetPath.isEmpty()) return "";
    
    // 1. 规范化路径并计算 16 位 Sha256 哈希指纹
    QString normPath = QDir::toNativeSeparators(mainAssetPath).toLower();
    QByteArray hash = QCryptographicHash::hash(normPath.toUtf8(), QCryptographicHash::Sha256).left(8).toHex().toUpper();
    
    // 2. 确保 .arcmeta/disk_thumbs/ 目录存在
    QString cacheDir = QCoreApplication::applicationDirPath() + "/.arcmeta/disk_thumbs";
    QDir().mkpath(cacheDir);

    return cacheDir + "/" + QString(hash) + ".png";
}

QImage CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(const QString& mainAssetPath) {
    QFileInfo fi(mainAssetPath);
    QString containerDir = fi.absolutePath();

    if (containerDir.endsWith(".arc", Qt::CaseInsensitive)) {
        // 1. .arc 胶囊模式：读取胶囊内部 <baseName>_thumbnail.png
        QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";
        if (QFile::exists(thumbPath)) {
            QImage img;
            if (img.load(thumbPath)) return img;
        }
    } else {
        // 2. 磁盘模式：读取 .arcmeta/disk_thumbs/ 哈希缓存
        QString diskCachePath = getDiskThumbCachePath(mainAssetPath);
        if (QFile::exists(diskCachePath)) {
            QImage img;
            if (img.load(diskCachePath)) return img;
        }
    }
    return QImage(); // 绝不实时提取
}

QImage CapsuleMediaExtractor::getCapsuleThumbnail(const QString& mainAssetPath, int size) {
    // 先尝试只读快速命中
    QImage cached = getCapsuleThumbnailReadOnly(mainAssetPath);
    if (!cached.isNull()) return cached;

    // 实时提取图像
    QFileInfo fi(mainAssetPath);
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        std::lock_guard<std::mutex> guiLock(CapsuleMediaExtractor::s_qtGuiMutex);
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

    // 区分双轨落盘
    if (!img.isNull()) {
        QString containerDir = fi.absolutePath();
        if (containerDir.endsWith(".arc", Qt::CaseInsensitive)) {
            // A 模式：写入 .arc 胶囊内部
            QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";
            img.save(thumbPath, "PNG");
        } else {
            // B 模式：统一写入 .arcmeta/disk_thumbs/ 目录
            QString diskCachePath = getDiskThumbCachePath(mainAssetPath);
            img.save(diskCachePath, "PNG");
        }
    }
    return img;
}

} // namespace ArcMeta
