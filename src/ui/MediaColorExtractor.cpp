#include "MediaColorExtractor.h"
#include "../core/AppConfig.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>

namespace ArcMeta {

bool MediaColorExtractor::isGraphicsFile(const QString& ext) {
    static const QStringList graphicsExts = {
        "png", "jpg", "jpeg", "bmp", "gif", "webp", "ico", "cur", "ani", "tiff", "tif",
        "psd", "psb", "ai", "eps", "pdf", "svg", "cdr",
        "sketch", "xd", "fig", "dwg", "dxf", "heic", "raw",
        "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm"
    };
    return graphicsExts.contains(ext.toLower());
}

bool MediaColorExtractor::isStandardImage(const QString& ext) {
    static const QStringList standardExts = {
        "png", "jpg", "jpeg", "bmp", "gif", "webp", "ico", "cur", "ani"
    };
    return standardExts.contains(ext.toLower());
}

QColor MediaColorExtractor::getExtensionColor(const QString& ext) {
    static QMap<QString, QColor> s_cache;
    QString upperExt = ext.toUpper();
    if (upperExt == "DIR") return QColor(45, 65, 85, 200);
    if (upperExt.isEmpty()) return QColor(60, 60, 60, 180);
    if (s_cache.contains(upperExt)) return s_cache[upperExt];

    QString settingKey = QString("ExtensionColors/%1").arg(upperExt);
    QVariant val = AppConfig::instance().getValue(settingKey);
    if (val.isValid()) {
        QColor color = val.value<QColor>();
        s_cache[upperExt] = color;
        return color;
    }

    size_t hash = qHash(upperExt);
    int hue = static_cast<int>(hash % 360);
    QColor color = QColor::fromHsl(hue, 160, 110, 200); 
    s_cache[upperExt] = color;
    AppConfig::instance().setValue(settingKey, color);
    return color;
}

QColor MediaColorExtractor::quantizeColor(const QColor& color) {
    return color;
}

QVector<QPair<QColor, float>> MediaColorExtractor::extractPalette(const QString& targetFile) {
    // 统一改为通过 ImageDecoderFacade 加载安全缩略图
    QImage targetImg = ImageDecoderFacade::loadScaledImage(targetFile, 256);
    if (targetImg.isNull()) return {};

    // 委托给 ColorAlgorithmEngine 处理
    return ColorAlgorithmEngine::extractPaletteFromImage(targetImg);
}

QColor MediaColorExtractor::extractDominantColor(const QString& targetFile) {
    auto palette = extractPalette(targetFile);
    return palette.isEmpty() ? QColor() : palette.first().first;
}

} // namespace ArcMeta
