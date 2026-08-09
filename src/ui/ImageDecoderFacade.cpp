#include "ImageDecoderFacade.h"
#include <QImageReader>

namespace ArcMeta {

QImage ImageDecoderFacade::loadScaledImage(const QString& filePath, int targetSize, int maxAllocationMB) {
    QImageReader reader(filePath);
    // 1. 强制设置单图分配配额 128MB
    reader.setAllocationLimit(maxAllocationMB);

    if (!reader.canRead()) return QImage();

    // 2. 预读原始尺寸
    QSize origSize = reader.size();
    if (origSize.isValid() && (origSize.width() > targetSize || origSize.height() > targetSize)) {
        // 3. 计算保持宽高比的缩放尺寸
        QSize scaledSize = origSize.scaled(targetSize, targetSize, Qt::KeepAspectRatio);
        // 4.【关键】在 read() 前调用 setScaledSize，驱动底层只分配小图内存！
        reader.setScaledSize(scaledSize);
    }

    // 5. 解码小图落盘内存
    return reader.read();
}

QSize ImageDecoderFacade::readImageDimensions(const QString& filePath) {
    QImageReader reader(filePath);
    if (!reader.canRead()) return QSize();
    return reader.size();
}

} // namespace ArcMeta
