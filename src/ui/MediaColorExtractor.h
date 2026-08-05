#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <QPair>

namespace ArcMeta {

struct LabColor {
    double l, a, b;
};

class MediaColorExtractor {
public:
    static bool isGraphicsFile(const QString& ext);
    static bool isStandardImage(const QString& ext);
    static QColor getExtensionColor(const QString& ext);
    static QColor quantizeColor(const QColor& color);
    
    static LabColor rgbToLab(const QColor& color);
    static double calculateDeltaE(const QColor& c1, const QColor& c2);
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile);
    static QColor extractDominantColor(const QString& targetFile);
    static QImage extractEmbeddedPsdThumbnail(const QString& path);
    static QImage extractEmbeddedAiPreview(const QString& path, int targetSize = 512);
    static QImage extractEmbeddedEpsPreview(const QString& path, int targetSize = 512);
    static QImage renderPdfAiFirstPage(const QString& filePath, int targetSize = 512);
    static QImage renderWithGhostscript(const QString& filePath, int targetSize = 512);
private:
    static QString findGhostscriptExecutable();
};

} // namespace ArcMeta
