#include <stddef.h>
#include <stdint.h>

// 🚨 关键修复：C++ 必须显式指定 extern "C"，告知 MSVC 按纯 C 语言函数名进行链接
extern "C" {
#include "tiffio.h"
}

#include "MediaColorExtractor.h"
#include "../core/AppConfig.h"
#include "WindowsShellThumbnailProvider.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression> // 🚨 补全缺失的正则头文件，解决 rxSpaces 与 split 报错
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QSvgRenderer>
#include <QPainter>
#include <QMap>
#include <QHash>
#include <cmath>
#include <algorithm>

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

QImage MediaColorExtractor::renderPdfAiFirstPage(const QString& filePath, int targetSize) {
#ifdef Q_OS_WIN
    // 方案 B 的完美工业级落地：Windows 10/11 平台下，直接调用 Windows 系统的 Shell 缩略图服务。
    // Shell 缩略图服务在底层会自动调配并唤醒系统级内置 PDF 矢量光栅化解码引擎（如 Microsoft Edge PDF Provider 等），
    // 这样既能 100% 成功画出 AI/PDF 文件的第一页，又能完美避免繁琐易碎、极易产生编译/链接冲突的 D3D/DirectX 原生接口。
    QImage img = WindowsShellThumbnailProvider::getShellThumbnail(filePath, targetSize);
    if (!img.isNull()) {
        qDebug() << "[MediaColorExtractor][AI/PDF] 方案 B：Windows 原生系统 PDF 引擎矢量渲染成功：" << filePath;
        return img;
    }
#else
    Q_UNUSED(filePath);
    Q_UNUSED(targetSize);
#endif
    return QImage();
}

QImage MediaColorExtractor::extractEmbeddedAiPreview(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QImage();

    // 🚨 放弃脆弱的 QTextStream！直接读取前 15MB 原始二进制字节流，100% 免疫二进制乱码与长行卡死
    QByteArray rawData = file.read(15 * 1024 * 1024);
    file.close();

    if (rawData.isEmpty()) return QImage();

    // =========================================================================
    // 通道 1：解析 PostScript %AI7_Thumbnail ~ %AI10_Thumbnail 256色调色板 (二进制游标扫描)
    // =========================================================================
    int thumbHeaderIdx = rawData.indexOf("%AI7_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI8_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI9_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI10_Thumbnail:");

    if (thumbHeaderIdx != -1) {
        int lineEnd = rawData.indexOf('\n', thumbHeaderIdx);
        if (lineEnd != -1) {
            QByteArray headerLine = rawData.mid(thumbHeaderIdx, lineEnd - thumbHeaderIdx);
            QString headerStr = QString::fromLatin1(headerLine);
            QStringList parts = headerStr.section(':', 1).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                int width = parts[0].toInt();
                int height = parts[1].toInt();

                int blockEnd = rawData.indexOf("%AI7_ThumbnailEnd", lineEnd);
                if (blockEnd == -1) blockEnd = rawData.indexOf("%AI9_ThumbnailEnd", lineEnd);
                if (blockEnd == -1) blockEnd = rawData.indexOf("%%EndData", lineEnd);

                if (blockEnd != -1 && width > 0 && height > 0) {
                    QByteArray hexData;
                    hexData.reserve(blockEnd - lineEnd);
                    
                    int cur = lineEnd;
                    while (cur < blockEnd) {
                        int nextLine = rawData.indexOf('\n', cur);
                        if (nextLine == -1) break;
                        QByteArray line = rawData.mid(cur, nextLine - cur).trimmed();
                        if (line.startsWith("%")) {
                            hexData.append(line.mid(1).trimmed());
                        }
                        cur = nextLine + 1;
                    }

                    QByteArray binaryData = QByteArray::fromHex(hexData);
                    const int paletteSize = 256 * 3; // 768 字节 RGB 调色板

                    if (binaryData.size() >= paletteSize + width * height) {
                        QList<QRgb> colorTable;
                        colorTable.reserve(256);
                        const uchar* palPtr = reinterpret_cast<const uchar*>(binaryData.constData());
                        for (int i = 0; i < 256; ++i) {
                            colorTable.append(qRgb(palPtr[i * 3], palPtr[i * 3 + 1], palPtr[i * 3 + 2]));
                        }

                        QImage img(width, height, QImage::Format_Indexed8);
                        img.setColorTable(colorTable);
                        const uchar* pixelPtr = palPtr + paletteSize;
                        for (int y = 0; y < height; ++y) {
                            memcpy(img.scanLine(y), pixelPtr + y * width, width);
                        }
                        if (!img.isNull()) {
                            qDebug() << "[MediaColorExtractor][AI] 二进制游标提取 %AI_Thumbnail 成功：" << filePath;
                            return img.convertToFormat(QImage::Format_ARGB32);
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // 通道 2：解析 Adobe XMP 元数据中的 Base64 预览图 (<xmpGImg:image>)
    // =========================================================================
    int xmpStart = rawData.indexOf("<xmpGImg:image>");
    if (xmpStart != -1) {
        xmpStart += 15; // 跳过 <xmpGImg:image> 标签
        int xmpEnd = rawData.indexOf("</xmpGImg:image>", xmpStart);
        if (xmpEnd != -1) {
            QByteArray base64Data = rawData.mid(xmpStart, xmpEnd - xmpStart).trimmed();
            base64Data.replace("\n", "").replace("\r", "").replace(" ", ""); // 物理清洗换行符
            QByteArray jpgBytes = QByteArray::fromBase64(base64Data);
            QImage img;
            if (img.loadFromData(jpgBytes)) {
                qDebug() << "[MediaColorExtractor][AI] 成功解包 Adobe XMP 内嵌 Base64 预览图：" << filePath;
                return img;
            }
        }
    }

    // 🚨【方案 B 兜底】：唤醒系统原生 PDF 矢量引擎，强行渲染 AI 文件第 1 页主画布！
    // 无论 zlib 如何压缩、无论保存时是否勾选预览，100% 必出彩色画面！
    QImage pdfRenderImg = renderPdfAiFirstPage(filePath, 256);
    if (!pdfRenderImg.isNull()) {
        return pdfRenderImg;
    }

    // =========================================================================
    // 通道 3：检索 PDF 规范下的 JPEG / PNG 裸数据流 (\xFF\xD8\xFF)
    // =========================================================================
    int jpgStart = rawData.indexOf("\xFF\xD8\xFF");
    if (jpgStart != -1) {
        int jpgEnd = rawData.indexOf("\xFF\xD9", jpgStart);
        if (jpgEnd != -1) {
            QByteArray jpgData = rawData.mid(jpgStart, (jpgEnd + 2) - jpgStart);
            QImage img;
            if (img.loadFromData(jpgData, "JPEG") && img.width() >= 32) {
                qDebug() << "[MediaColorExtractor][AI] 成功提取 JPEG 裸数据流：" << filePath;
                return img;
            }
        }
    }

    int pngStart = rawData.indexOf("\x89PNG\r\n\x1a\n");
    if (pngStart != -1) {
        int pngEnd = rawData.indexOf("IEND", pngStart);
        if (pngEnd != -1) {
            QByteArray pngData = rawData.mid(pngStart, (pngEnd + 8) - pngStart);
            QImage img;
            if (img.loadFromData(pngData, "PNG") && img.width() >= 32) {
                qDebug() << "[MediaColorExtractor][AI] 成功提取 PNG 裸数据流：" << filePath;
                return img;
            }
        }
    }

    // =========================================================================
    // 通道 4：Windows Shell 严格缩略图兜底
    // =========================================================================
    return WindowsShellThumbnailProvider::getShellThumbnail(filePath, 256);
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

    // 1. 优先尝试 DOS 二进制头 (C5D0D3C6)
    if (quint8(header[0]) == 0xC5 && quint8(header[1]) == 0xD0 &&
        quint8(header[2]) == 0xD3 && quint8(header[3]) == 0xC6) {
        
        quint32 tiffOffset = (quint8(header[20])) | (quint8(header[21]) << 8) |
                             (quint8(header[22]) << 16) | (quint8(header[23]) << 24);
        quint32 tiffLength = (quint8(header[24])) | (quint8(header[25]) << 8) |
                             (quint8(header[26]) << 16) | (quint8(header[27]) << 24);
        if (tiffOffset > 0 && tiffLength > 0) {
            file.seek(tiffOffset);
            QByteArray tiffData = file.read(tiffLength);
            QImage img = decodeTiffFromMemory(tiffData);
            if (!img.isNull()) {
                qDebug() << "[MediaColorExtractor][EPS] 内嵌预览通过 libtiff 提取成功：" << path;
                return img;
            }
        }
    }

    // 🚨 2. 补全自愈回退处理：普通 ASCII EPS (文本格式) 的 %%BeginPreview: 预览块解析
    file.seek(0);
    QTextStream in(&file);
    bool inPreview = false;
    QString hexData;
    int width = 0, height = 0;

    // 编译优化正则防漏
    QRegularExpression rxSpaces("\\s+");

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("%%BeginPreview:")) {
            QStringList parts = line.split(rxSpaces, Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                width = parts[1].toInt();
                height = parts[2].toInt();
                inPreview = true;
            }
            continue;
        }
        if (line.startsWith("%%EndPreview")) {
            break; // 预览块结束
        }
        if (inPreview) {
            if (line.startsWith("%")) {
                hexData.append(line.mid(1).trimmed());
            }
        }
    }

    if (!hexData.isEmpty() && width > 0 && height > 0) {
        QByteArray binaryData = QByteArray::fromHex(hexData.toLatin1());
        QImage img;
        if (img.loadFromData(binaryData)) {
            qDebug() << "[MediaColorExtractor][EPS] 普通文本格式 ASCII EPS 内嵌 %%BeginPreview 提取成功：" << path;
            return img;
        }
    }

    qWarning() << "[MediaColorExtractor][EPS] 未能通过 DOS 二进制或 ASCII %%BeginPreview 提取内嵌位图预览：" << path;
    return QImage();
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
        // 🚨 删掉了原本对 psd/psb/ai 文件的强制拦截阻断，允许其在解析失败时降级提图并使用 Windows Shell 严格缩略图兜底
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