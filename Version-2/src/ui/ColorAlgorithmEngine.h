#pragma once

#include <QColor>
#include <QImage>
#include <QVector>
#include <QPair>

namespace ArcMeta {

struct LabColor {
    double l, a, b;
};

class ColorAlgorithmEngine {
public:
    static LabColor rgbToLab(const QColor& color);
    static double calculateDeltaE(const QColor& c1, const QColor& c2);
     
    // 关键改变：直接接收已被降采样（如 200x200）的 QImage 句柄，禁止传入文件路径
    static QVector<QPair<QColor, float>> extractPaletteFromImage(const QImage& preScaledImage);
    static QColor extractDominantColorFromImage(const QImage& preScaledImage);
};

} // namespace ArcMeta
