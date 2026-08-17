#include "ImageDecoderFacade.h"
#include "FormatDecoders.h"
#include <QFileInfo>
#include <QFile>
#include <QImageReader>

namespace ArcMeta {

QImage ImageDecoderFacade::loadScaledImage(const QString& filePath, int targetSize, int maxAllocationMB) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();

    // 1. 路由至专用格式解码器 (PSD / AI / EPS)
    if (ext == "psd" || ext == "psb") {
        return FormatDecoders::extractPsdHeaderThumbnail(filePath);
    }
    if (ext == "ai" || ext == "pdf") {
        return FormatDecoders::extractAiPreview(filePath, targetSize);
    }
    if (ext == "eps") {
        return FormatDecoders::extractEpsPreview(filePath, targetSize);
    }
    if (ext == "tif" || ext == "tiff") {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly)) {
            return FormatDecoders::decodeTiffMemorySafely(f.readAll(), 64);
        }
    }

    // 2. 普通图像 (PNG / JPG / WEBP / BMP 等) 走 QImageReader 降采样加载
    QImageReader reader(filePath);
    reader.setAllocationLimit(maxAllocationMB);
     
    if (!reader.canRead()) return QImage();
     
    QSize origSize = reader.size();
    if (origSize.isValid() && (origSize.width() > targetSize || origSize.height() > targetSize)) {
        QSize scaledSize = origSize.scaled(targetSize, targetSize, Qt::KeepAspectRatio);
        reader.setScaledSize(scaledSize);
    }
     
    return reader.read();
}

QSize ImageDecoderFacade::readImageDimensions(const QString& filePath) {
    QImageReader reader(filePath);
    if (!reader.canRead()) return QSize();
    return reader.size();
}

} // namespace ArcMeta
