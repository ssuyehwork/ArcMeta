#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <QPair>
#include "FormatDecoders.h"
#include "ImageDecoderFacade.h"
#include "ColorAlgorithmEngine.h"

namespace ArcMeta {

class MediaColorExtractor {
public:
    static bool isGraphicsFile(const QString& ext);
    static bool isStandardImage(const QString& ext);
    static QColor getExtensionColor(const QString& ext);
    static QColor quantizeColor(const QColor& color);
    
    static LabColor rgbToLab(const QColor& color) {
        auto res = ColorAlgorithmEngine::rgbToLab(color);
        return {res.l, res.a, res.b};
    }
    
    static double calculateDeltaE(const QColor& c1, const QColor& c2) {
        return ColorAlgorithmEngine::calculateDeltaE(c1, c2);
    }
    
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile);
    static QColor extractDominantColor(const QString& targetFile);
    
    static QImage extractEmbeddedPsdThumbnail(const QString& path) {
        return FormatDecoders::extractPsdHeaderThumbnail(path);
    }
    
    static QImage extractEmbeddedAiPreview(const QString& path, int targetSize = 512) {
        return FormatDecoders::extractAiPreview(path, targetSize);
    }
    
    static QImage extractEmbeddedEpsPreview(const QString& path, int targetSize = 512) {
        return FormatDecoders::extractEpsPreview(path, targetSize);
    }
    
    static QImage renderWithGhostscript(const QString& filePath, int targetSize = 512) {
        return FormatDecoders::renderGhostscriptSafely(filePath, targetSize);
    }
};

} // namespace ArcMeta
