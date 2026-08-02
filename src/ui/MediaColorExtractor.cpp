#include <stddef.h>
#include <stdint.h>

// 🚨 关键修复：C++ 必须显式指定 extern "C"，告知 MSVC 按纯 C 语言函数名进行链接
extern "C" {
#include "tiffio.h"
}

#include "MediaColorExtractor.h"
#include "../core/AppConfig.h"
#include "WindowsShellThumbnailProvider.h"

// 自定义内存读取结构，用来在内存中模拟文件读取
struct TiffMemoryStream {
    const char* data;
    tmsize_t size;
    tmsize_t offset;
};

// 内存读取回调函数
static tmsize_t tiffReadProc(thandle_t clientData, void* buf, tmsize_t size) {
    auto stream = reinterpret_cast<TiffMemoryStream*>(clientData);
    if (stream->offset + size > stream->size) {
        size = stream->size - stream->offset;
    }
    if (size > 0) {
        memcpy(buf, stream->data + stream->offset, size);
        stream->offset += size;
    }
    return size;
}

static tmsize_t tiffWriteProc(thandle_t, void*, tmsize_t) {
    return 0;
}

static toff_t tiffSeekProc(thandle_t clientData, toff_t off, int whence) {
    auto stream = reinterpret_cast<TiffMemoryStream*>(clientData);
    switch (whence) {
        case SEEK_SET: stream->offset = off; break;
        case SEEK_CUR: stream->offset += off; break;
        case SEEK_END: stream->offset = stream->size + off; break;
    }
    return stream->offset;
}

static int tiffCloseProc(thandle_t) {
    return 0;
}

static toff_t tiffSizeProc(thandle_t clientData) {
    return reinterpret_cast<TiffMemoryStream*>(clientData)->size;
}

static int tiffMapProc(thandle_t clientData, void** pbase, toff_t* psize) {
    auto stream = reinterpret_cast<TiffMemoryStream*>(clientData);
    *pbase = const_cast<char*>(stream->data);
    *psize = stream->size;
    return 1;
}

static void tiffUnmapProc(thandle_t, void*, toff_t) {
}

static QImage decodeTiffFromMemory(const QByteArray& tiffData) {
    TiffMemoryStream stream;
    stream.data = tiffData.constData();
    stream.size = tiffData.size();
    stream.offset = 0;

    TIFF* tif = TIFFClientOpen("MemoryTIFF", "r",
                               reinterpret_cast<thandle_t>(&stream),
                               tiffReadProc, tiffWriteProc,
                               tiffSeekProc, tiffCloseProc,
                               tiffSizeProc, tiffMapProc, tiffUnmapProc);
    if (!tif) {
        return QImage();
    }

    uint32_t width = 0, height = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

    if (width == 0 || height == 0) {
        TIFFClose(tif);
        return QImage();
    }

    QImage img(width, height, QImage::Format_ARGB32);
    if (img.isNull()) {
        TIFFClose(tif);
        return QImage();
    }

    // 使用 TIFFReadRGBAImageOriented 读入，它的原点可以定位在 Top-Left (1)
    if (!TIFFReadRGBAImageOriented(tif, width, height,
                                  reinterpret_cast<uint32_t*>(img.bits()),
                                  ORIENTATION_TOPLEFT, 0)) {
        TIFFClose(tif);
        return QImage();
    }

    TIFFClose(tif);
    return img;
}
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QSvgRenderer>
#include <QPainter>
#include <QMap>
#include <QHash>
#include <cmath>
#include <algorithm>

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

QString MediaColorExtractor::diskThumbCachePath(const QString& path, int size) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir(appDir).filePath(".arcmeta/disk_thumbs/");
    QDir().mkpath(cacheDir);
#ifdef Q_OS_WIN
    SetFileAttributesW(QDir(appDir).filePath(".arcmeta").toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    QString safeName = QString::number(qHash(hashKey), 16) + ".png";
    return cacheDir + safeName;
}

QColor MediaColorExtractor::quantizeColor(const QColor& color) {
    return color;
}

LabColor MediaColorExtractor::rgbToLab(const QColor& color) {
    double r = color.red() / 255.0;
    double g = color.green() / 255.0;
    double b = color.blue() / 255.0;

    r = (r > 0.04045) ? std::pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = (g > 0.04045) ? std::pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = (b > 0.04045) ? std::pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    r *= 100.0; g *= 100.0; b *= 100.0;

    double x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    double y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    double z = r * 0.0193 + g * 0.1192 + b * 0.9505;

    x /= 95.047;
    y /= 100.000;
    z /= 108.883;

    auto f = [](double t) {
        return (t > 0.008856) ? std::pow(t, 1.0/3.0) : (7.787 * t) + (16.0/116.0);
    };

    double L = (116.0 * f(y)) - 16.0;
    double A = 500.0 * (f(x) - f(y));
    double B = 200.0 * (f(y) - f(z));

    return {L, A, B};
}

double MediaColorExtractor::calculateDeltaE(const QColor& c1, const QColor& c2) {
    if (!c1.isValid() || !c2.isValid()) return 1000.0;
    LabColor l1 = rgbToLab(c1);
    LabColor l2 = rgbToLab(c2);
    return std::sqrt(std::pow(l1.l - l2.l, 2) + std::pow(l1.a - l2.a, 2) + std::pow(l1.b - l2.b, 2));
}

QImage MediaColorExtractor::extractEmbeddedPsdThumbnail(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QImage();

    // PSD 头部固定 26 字节之后是"颜色模式数据段"，其长度可变，再往后才是"图像资源块"
    QByteArray header = file.read(26);
    if (header.size() < 26 || !header.startsWith("8BPS")) return QImage();

    quint32 colorModeLen = 0;
    {
        QByteArray lenBytes = file.read(4);
        if (lenBytes.size() < 4) return QImage();
        colorModeLen = (quint8(lenBytes[0]) << 24) | (quint8(lenBytes[1]) << 16) |
                       (quint8(lenBytes[2]) << 8) | quint8(lenBytes[3]);
    }
    file.seek(file.pos() + colorModeLen);

    QByteArray resLenBytes = file.read(4);
    if (resLenBytes.size() < 4) return QImage();
    quint32 resSectionLen = (quint8(resLenBytes[0]) << 24) | (quint8(resLenBytes[1]) << 16) |
                             (quint8(resLenBytes[2]) << 8) | quint8(resLenBytes[3]);

    qint64 resSectionEnd = file.pos() + resSectionLen;
    while (file.pos() < resSectionEnd) {
        QByteArray sig = file.read(4);
        if (sig != "8BIM") break;

        QByteArray idBytes = file.read(2);
        if (idBytes.size() < 2) break;
        quint16 resId = (quint8(idBytes[0]) << 8) | quint8(idBytes[1]);

        quint8 nameLen = 0;
        file.getChar(reinterpret_cast<char*>(&nameLen));
        file.seek(file.pos() + nameLen + ((nameLen % 2 == 0) ? 1 : 0)); // 名称按偶数字节对齐

        QByteArray dataLenBytes = file.read(4);
        if (dataLenBytes.size() < 4) break;
        quint32 dataLen = (quint8(dataLenBytes[0]) << 24) | (quint8(dataLenBytes[1]) << 16) |
                           (quint8(dataLenBytes[2]) << 8) | quint8(dataLenBytes[3]);

        // 资源 ID 1036 (0x040C) = 缩略图资源 (RGB, 内嵌标准 JPEG)
        if (resId == 0x040C) {
            if (dataLen < 28) break;
            file.seek(file.pos() + 28); // 跳过缩略图头部固定 28 字节（格式/宽高/位深等字段）
            QByteArray jpegData = file.read(dataLen - 28);
            QImage img;
            if (img.loadFromData(jpegData, "JPEG")) {
                return img;
            }
            break;
        }

        file.seek(file.pos() + dataLen + (dataLen % 2)); // 数据同样按偶数字节对齐
    }
    return QImage();
}

QImage MediaColorExtractor::extractEmbeddedAiPreview(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MediaColorExtractor][AI] 文件打开失败：" << filePath;
        return QImage();
    }

    // 采用高效的流式分块搜寻游标，消除 5MB 的硬编码限制，支持无限大文件的内嵌预览检索
    const qint64 chunkSize = 1024 * 1024; // 1MB chunk size
    QByteArray buffer;
    qint64 startOffset = -1;

    while (!file.atEnd()) {
        qint64 currentChunkOffset = file.pos();
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty()) break;

        // 如果之前有残留 buffer（重叠），拼接以防标记被分块截断
        if (!buffer.isEmpty()) {
            buffer = buffer.right(2) + chunk;
            currentChunkOffset -= 2;
        } else {
            buffer = chunk;
        }

        int idx = buffer.indexOf("\xFF\xD8\xFF");
        if (idx != -1) {
            startOffset = currentChunkOffset + idx;
            break;
        }
    }

    if (startOffset == -1) {
        qWarning() << "[MediaColorExtractor][AI] 未找到 JPEG 起始标记(FFD8FF)，该文件可能未内嵌兼容性预览：" << filePath;
        return QImage();
    }

    // 从 startOffset 开始寻找 \xFF\xD9 结束标记
    if (!file.seek(startOffset)) {
        qWarning() << "[MediaColorExtractor][AI] 重定向文件指针失败：" << filePath;
        return QImage();
    }

    QByteArray imgData;
    buffer.clear();
    bool foundEnd = false;

    while (!file.atEnd() && imgData.size() < 50 * 1024 * 1024) { // 安全上限：最多提取 50MB 的 JPEG 数据
        qint64 currentChunkOffset = file.pos();
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty()) break;

        if (!buffer.isEmpty()) {
            buffer = buffer.right(1) + chunk;
            currentChunkOffset -= 1;
        } else {
            buffer = chunk;
        }

        int idx = buffer.indexOf("\xFF\xD9");
        if (idx != -1) {
            qint64 endOffset = currentChunkOffset + idx + 2;
            qint64 totalLen = endOffset - startOffset;
            if (totalLen > 0) {
                if (file.seek(startOffset)) {
                    imgData = file.read(totalLen);
                    foundEnd = true;
                }
            }
            break;
        }
    }

    file.close();

    if (!foundEnd || imgData.isEmpty()) {
        qWarning() << "[MediaColorExtractor][AI] 找到起始标记但未找到 JPEG 结束标记(FFD9)或数据不完整：" << filePath;
        return QImage();
    }

    QImage img;
    if (!img.loadFromData(imgData)) {
        qWarning() << "[MediaColorExtractor][AI] 已提取出 JPEG 字节流但解码失败，数据长度：" << imgData.size() << "：" << filePath;
        return QImage();
    }

    qDebug() << "[MediaColorExtractor][AI] 内嵌预览提取成功：" << filePath;
    return img;
}

QImage MediaColorExtractor::extractEmbeddedEpsPreview(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MediaColorExtractor][EPS] 文件打开失败：" << path;
        return QImage();
    }

    QByteArray header = file.read(30);
    if (header.size() < 30) {
        qWarning() << "[MediaColorExtractor][EPS] 文件头不足 30 字节：" << path;
        return QImage();
    }

    if (quint8(header[0]) != 0xC5 || quint8(header[1]) != 0xD0 ||
        quint8(header[2]) != 0xD3 || quint8(header[3]) != 0xC6) {
        qWarning() << "[MediaColorExtractor][EPS] 不是 DOS EPS 格式（缺少 C5D0D3C6 魔数），该文件可能是普通 EPS/纯 PostScript，无内嵌位图预览可提取：" << path;
        return QImage();
    }

    quint32 tiffOffset = (quint8(header[20])) | (quint8(header[21]) << 8) |
                         (quint8(header[22]) << 16) | (quint8(header[23]) << 24);
    quint32 tiffLength = (quint8(header[24])) | (quint8(header[25]) << 8) |
                         (quint8(header[26]) << 16) | (quint8(header[27]) << 24);
    if (tiffOffset == 0 || tiffLength == 0) {
        qWarning() << "[MediaColorExtractor][EPS] DOS EPS 魔数匹配，但 TIFF 预览偏移/长度字段为 0，该文件未内嵌 TIFF 预览：" << path;
        return QImage();
    }

    file.seek(tiffOffset);
    QByteArray tiffData = file.read(tiffLength);
    QImage img = decodeTiffFromMemory(tiffData);
    if (img.isNull()) {
        qWarning() << "[MediaColorExtractor][EPS] 已定位到 TIFF 数据区但通过 libtiff 解码失败，长度：" << tiffData.size() << "：" << path;
        return QImage();
    }

    qDebug() << "[MediaColorExtractor][EPS] 内嵌预览通过 libtiff 提取成功：" << path;
    return img;
}

QImage MediaColorExtractor::getImageForAnalysis(const QString& path, int size) {
    QString cachePath = diskThumbCachePath(path, size);
    if (QFile::exists(cachePath)) {
        QImage cached;
        if (cached.load(cachePath)) return cached;
    }

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(path);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
        }
    } else if (ext == "psd" || ext == "psb") {
        img = extractEmbeddedPsdThumbnail(path);
    } else if (ext == "ai") {
        img = extractEmbeddedAiPreview(path);
    } else if (ext == "eps") {
        img = extractEmbeddedEpsPreview(path);
    }

    if (img.isNull()) {
        // 🚨 极致物理重构：针对 psd/psb/ai/eps 这四类矢量和设计层文件，若内嵌数据流解析失败，
        // 绝对禁止采用系统关联的软件大图标（如 Ai 大图标）进行虚假兜底！
        static const QStringList rawDesignExts = {"psd", "psb", "ai", "eps"};
        if (rawDesignExts.contains(ext)) {
            qWarning() << "[MediaColorExtractor][BLOCK] 已强制拦截该文件的系统默认图标兜底，标记为无真实内容预览：" << path;
            return QImage(); // 干净地返回空图
        }

        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    if (!img.isNull()) {
        img.save(cachePath, "PNG");
    }
    return img;
}

QVector<QPair<QColor, float>> MediaColorExtractor::extractPalette(const QString& targetFile) {
    QImage targetImg = getImageForAnalysis(targetFile, 256);
    if (targetImg.isNull()) return {};

    QImage sampled = targetImg.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    struct BucketInfo { 
        long long rSum = 0, gSum = 0, bSum = 0; 
        double rankWeight = 0.0;
        int count = 0; 
    };
    QMap<QRgb, BucketInfo> bucketStats;
    int totalPixels = 0;

    for (int row = 0; row < sampled.height(); ++row) {
        for (int col = 0; col < sampled.width(); ++col) {
            QRgb rgb = sampled.pixel(col, row);
            if (qAlpha(rgb) < 128) continue;

            int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
            QColor color(r, g, b);
            int h, s, l; color.getHsl(&h, &s, &l);
            double sat = s / 255.0, lig = l / 255.0;

            double centerX = sampled.width() / 2.0;
            double centerY = sampled.height() / 2.0;
            double maxDist = std::sqrt(centerX * centerX + centerY * centerY);
            double dist = std::sqrt(std::pow(col - centerX, 2) + std::pow(row - centerY, 2));
            double spatialWeight = 1.0 + (1.0 - dist / maxDist) * 0.5;

            double vibrancy = sat * (1.0 - std::abs(lig - 0.5) * 2.0);
            double weight = (0.5 + 4.0 * std::pow(vibrancy, 1.5)) * spatialWeight;

            if (lig > 0.95 && sat < 0.05) {
                weight = 0.001;
            } else if (lig < 0.15) {
                weight = 2.0 * spatialWeight;
            }

            QRgb rgbKey = qRgb(r & 0xF8, g & 0xF8, b & 0xF8);
            auto& stat = bucketStats[rgbKey];
            stat.rSum += r; stat.gSum += g; stat.bSum += b;
            stat.rankWeight += weight;
            stat.count++;
            totalPixels++;
        }
    }
    if (totalPixels == 0) return {};

    struct FinalBucket { QColor avgColor; double rankWeight; int count; };
    QList<FinalBucket> buckets;
    for (auto it = bucketStats.begin(); it != bucketStats.end(); ++it) {
        const auto& s = it.value();
        buckets.append({ QColor((int)(s.rSum / s.count), (int)(s.gSum / s.count), (int)(s.bSum / s.count)), s.rankWeight, s.count });
    }

    QList<FinalBucket> merged;
    for (const auto& b : buckets) {
        bool found = false;
        for (auto& m : merged) {
            double de = calculateDeltaE(b.avgColor, m.avgColor);
            if (de < 10.0) {
                int total = m.count + b.count;
                m.avgColor = QColor(
                    (int)(m.avgColor.red() * m.count + b.avgColor.red() * b.count) / total,
                    (int)(m.avgColor.green() * m.count + b.avgColor.green() * b.count) / total,
                    (int)(m.avgColor.blue() * m.count + b.avgColor.blue() * b.count) / total
                );
                m.rankWeight += b.rankWeight; m.count = total;
                found = true; break;
            }
        }
        if (!found) merged.append(b);
    }

    QVector<QPair<QColor, float>> result;
    struct Candidate { QColor color; double score; int count; };
    QList<Candidate> candidates;
    for (const auto& m : merged) {
        candidates.append({ m.avgColor, m.rankWeight, m.count });
    }

    while (result.size() < 10 && !candidates.isEmpty()) {
        int bestIdx = -1; double maxScore = -1e9;
        for (int i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            double score = c.score;
            
            for (const auto& r : result) {
                double de = calculateDeltaE(c.color, r.first);
                if (de < 20.0) {
                    score *= 0.01;
                } else if (de < 45.0) {
                    score *= (de / 45.0) * 0.5;
                }
            }
            
            if (score > maxScore) { maxScore = score; bestIdx = i; }
        }
        if (bestIdx != -1 && maxScore > 0) {
            result.append({ candidates[bestIdx].color, (float)candidates[bestIdx].count / totalPixels });
            candidates.removeAt(bestIdx);
        } else break;
    }
    return result;
}

QColor MediaColorExtractor::extractDominantColor(const QString& targetFile) {
    auto palette = extractPalette(targetFile);
    return palette.isEmpty() ? QColor() : palette.first().first;
}

} // namespace ArcMeta
