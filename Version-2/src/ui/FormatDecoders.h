#pragma once

#include <QImage>
#include <QString>
#include <QByteArray>

namespace ArcMeta {

class FormatDecoders {
public:
    // TIFF 物理内存解码（含安全防御）
    static QImage decodeTiffMemorySafely(const QByteArray& tiffData, int maxMemoryMB = 64);
     
    // PSD 嵌套缩略图提取
    static QImage extractPsdHeaderThumbnail(const QString& filePath);
     
    // AI 嵌套预览图与 XMP 提取
    static QImage extractAiPreview(const QString& filePath, int targetSize = 512);
     
    // EPS 预览图与 Ghostscript 矢量渲染
    static QImage extractEpsPreview(const QString& filePath, int targetSize = 512);
     
    // External Process: Ghostscript 降采样渲染
    static QImage renderGhostscriptSafely(const QString& filePath, int targetSize = 512);

private:
    static QString findGhostscriptExecutable();
    static QImage renderPdfAiFirstPage(const QString& filePath, int targetSize = 512);
};

} // namespace ArcMeta
