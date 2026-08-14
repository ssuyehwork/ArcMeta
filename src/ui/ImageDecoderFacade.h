#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace ArcMeta {

class ImageDecoderFacade {
public:
    // 统一对外图像加载接口：防爆内存、强制预缩放
    static QImage loadScaledImage(const QString& filePath, int targetSize = 512, int maxAllocationMB = 128);
     
    // 仅提取图像物理宽高，0 内存分配
    static QSize readImageDimensions(const QString& filePath);
};

} // namespace ArcMeta
