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
    static QImage getImageForAnalysis(const QString& path, int size = 256);
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile);
    static QColor extractDominantColor(const QString& targetFile);
};

} // namespace ArcMeta
