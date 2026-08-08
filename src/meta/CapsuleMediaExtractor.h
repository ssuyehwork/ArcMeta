#ifndef CAPSULEMEDIAEXTRACTOR_H
#define CAPSULEMEDIAEXTRACTOR_H

#include <QImage>
#include <QString>
#include <mutex>

namespace ArcMeta {

class CapsuleMediaExtractor {
public:
    // 全局唯一：串行化保护一切会触碰Qt Gui/SVG内部状态的代码段
    // （QSvgRenderer、QPainter、QPixmap等），跨MediaExtractorPipeline与
    // CapsuleMediaExtractor共用同一把锁，防止多个worker线程并发触碰
    // Qt6Gui.dll内部非线程安全缓存导致崩溃（进程会以0xC0000005访问冲突退出）
    static std::mutex s_qtGuiMutex;

    // UI 热路径专属：只读已有缩略图（支持 .arc 胶囊内与 disk_thumbs 缓存）
    static QImage getCapsuleThumbnailReadOnly(const QString& mainAssetPath);

    // 后台管道提取与落盘缓存
    static QImage getCapsuleThumbnail(const QString& mainAssetPath, int size = 512);

    // 计算磁盘模式缩略图的哈希缓存路径
    static QString getDiskThumbCachePath(const QString& mainAssetPath);
};

} // namespace ArcMeta

#endif // CAPSULEMEDIAEXTRACTOR_H
