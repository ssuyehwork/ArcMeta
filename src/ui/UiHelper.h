#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once

#include <QIcon>
#include <QString>
#include <QColor>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QCache>
#include "../core/AppConfig.h"
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <QSet>
#include <QCoreApplication>
#include <QWidget>
#include <QBuffer>
#include <QProcess>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <algorithm>
#include <cmath>

// Windows Shell 缩略图引擎依赖
#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#ifdef __MINGW32__
// MinGW 可能不支持某些高级 Shell API
#include <shlwapi.h>
#else
#include <shobjidl_core.h>
#include <thumbcache.h>
#endif
#endif

#include "SvgIcons.h"

namespace ArcMeta {

/**
 * @brief UI 辅助类 (全量热加载版 - 杜绝懒加载)
 */
class UiHelper {
public:
    static QMap<QString, QPixmap>& iconPixmapCache() {
        static QMap<QString, QPixmap> cache;
        return cache;
    }

    static void initializeHotIcons() {
        qDebug() << "[UiHelper] 图标系统已启用懒加载模式";
    }

    static QColor parseColorName(const QString& colorName) {
        if (colorName.isEmpty()) return QColor();
        
        // 优先尝试原生解析 (支持 #RRGGBB)
        QColor c(colorName);
        if (c.isValid()) return c;

        if (colorName == "red" || colorName == "红") return QColor("#E24B4A");
        if (colorName == "orange" || colorName == "橙") return QColor("#EF9F27");
        if (colorName == "yellow" || colorName == "黄") return QColor("#FECF0E");
        if (colorName == "green" || colorName == "绿") return QColor("#639922");
        if (colorName == "cyan" || colorName == "青") return QColor("#1D9E75");
        if (colorName == "blue" || colorName == "蓝") return QColor("#378ADD");
        if (colorName == "purple" || colorName == "紫") return QColor("#7F77DD");
        if (colorName == "gray" || colorName == "灰") return QColor("#5F5E5A");
        if (colorName == "black" || colorName == "黑") return QColor("#000000");
        if (colorName == "white" || colorName == "白") return QColor("#FFFFFF");
        
        return QColor();
    }


    static QPixmap renderIcon(const QString& key, const QSize& size, const QColor& color) {
        if (!SvgIcons::icons.contains(key)) return QPixmap();
        QString svgData = SvgIcons::icons[key];
        svgData.replace("currentColor", color.name());
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(svgData.toUtf8());
        renderer.render(&painter);
        return pixmap;
    }

    static QString getSvgDataUrl(const QString& key, const QColor& color = QColor("#3498db")) {
        // [PHYSICAL COMPATIBILITY] 转换为 PNG Base64 以确保 QSS 100% 渲染成功
        // 2026-06-xx 物理修正：使用 20x20 尺寸以匹配 QTreeView 默认分支宽度
        QPixmap pix = renderIcon(key, QSize(20, 20), color);
        if (pix.isNull()) return QString();
        
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        pix.save(&buffer, "PNG");
        return QString("data:image/png;base64,%1").arg(QString(ba.toBase64()));
    }

    static QString getSvgTempFilePath(const QString& key, const QColor& color) {
        QPixmap pix = renderIcon(key, QSize(20, 20), color);
        if (pix.isNull()) return QString();

        // 2026-06-xx 物理修复：在路径中加入 V3 标识并强制覆盖。
        // 核心修正：Qt QSS 必须使用正斜杠 (/)，反斜杠会被转义导致加载失败。强制转换为正斜杠。
        QString tmpPath = QDir::temp().filePath(
            QString("arcmeta_%1_%2_v3.png").arg(key).arg(color.name().mid(1))
        );
        pix.save(tmpPath, "PNG");
        return QDir::fromNativeSeparators(tmpPath);
    }

    static bool isGraphicsFile(const QString& ext) {
        // 2026-06-xx 工业级扩容：只要 Windows 能显示预览图，就允许进入解析流程
        static const QStringList graphicsExts = {
            "png", "jpg", "jpeg", "bmp", "gif", "webp", "ico", "tiff", "tif",
            "psd", "psb", "ai", "eps", "pdf", "svg", "cdr",
            "sketch", "xd", "fig", "dwg", "dxf", "heic", "raw"
        };
        return graphicsExts.contains(ext.toLower());
    }

    static QIcon getIcon(const QString& key, const QColor& color, int size = 18) {
        QIcon icon;
        QPixmap pix = getPixmap(key, QSize(size, size), color);
        if (!pix.isNull()) icon.addPixmap(pix);
        return icon;
    }

    static QIcon getFileIcon(const QString& filePath, int size = 18, const QColor& overrideColor = QColor()) {
        Q_UNUSED(overrideColor);
        Q_UNUSED(size);
        
        QFileInfo info(filePath);
        // 2026-06-xx 架构修正：磁盘根目录图标应独立缓存，防止其覆盖通用文件夹图标
        QString key = info.isDir() ? (info.isRoot() ? filePath : "folder") : info.suffix().toLower();
        if (key.length() > 128) key = "unknown";
        
        static QMap<QString, QIcon> s_iconCache;
        if (s_iconCache.contains(key)) {
            return s_iconCache[key];
        }

        QFileIconProvider provider;
        QIcon icon;
        if (info.isDir()) {
            // 2026-06-xx 架构修正：判断是否为磁盘根目录
            if (info.isRoot()) {
                // 若是磁盘根目录，必须获取其盘符图标而非通用文件夹图标
                icon = provider.icon(info);
            } else {
                icon = provider.icon(QFileIconProvider::Folder);
            }
        } else {
            icon = provider.icon(QFileInfo("dummy." + key));
            if (icon.isNull()) {
                icon = provider.icon(QFileIconProvider::File);
            }
        }
        
        s_iconCache[key] = icon;
        return icon;
    }

    static QPixmap getPixmap(const QString& key, const QSize& size, const QColor& color) {
        QString cKey = QString("%1_%2_%3_%4").arg(key).arg(size.width()).arg(size.height()).arg(color.rgba());
        if (iconPixmapCache().contains(cKey)) return iconPixmapCache()[cKey];
        QPixmap rendered = renderIcon(key, size, color);
        if (!rendered.isNull()) iconPixmapCache().insert(cKey, rendered);
        return rendered;
    }

    static void applyMenuStyle(QWidget* menu) {
        if (!menu) return;
        // 2026-06-xx 物理修复：开启透明背景属性，消除圆角外的直角溢出
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlag(Qt::FramelessWindowHint);
        menu->setStyleSheet(
            "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; border-radius: 8px; }"
            "QMenu::item { padding: 6px 25px 6px 10px; border-radius: 4px; font-size: 12px; }"
            "QMenu::item:selected { background-color: #3E3E42; color: white; }"
            "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }"
            "QMenu::right-arrow { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjRUVFRUVFIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBvbHlsaW5lIHBvaW50cz0iOSAxOCAxNSAxMiA5IDYiPjwvcG9seWxpbmU+PC9zdmc+); width: 12px; height: 12px; right: 8px; }"
        );
    }

    static QColor getExtensionColor(const QString& ext) {
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

    /**
     * @brief 对颜色进行量化 (已废除破坏性位截断，直接返回原色以确保预览色与上色完全一致)
     */
    static inline QColor quantizeColor(const QColor& color) {
        return color;
    }


    /**
     * @brief 从图像中提取调色盘 (对标 Eagle 质量版)
     */
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile) {
        QImage targetImg = getShellThumbnail(targetFile, 256);
        if (targetImg.isNull()) targetImg.load(targetFile);
        if (targetImg.isNull()) return {};

        // 4. 采样分辨率从 128x128 提升到 200x200，增加低频特征色的采样覆盖
        QImage sampled = targetImg.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        struct BucketInfo { 
            long long rSum = 0, gSum = 0, bSum = 0; 
            double weightedCount = 0.0;
            int absoluteCount = 0;
        };
        QMap<QRgb, BucketInfo> bucketStats;
        double totalWeightedPixels = 0.0;

        for (int row = 0; row < sampled.height(); ++row) {
            for (int col = 0; col < sampled.width(); ++col) {
                QRgb rgb = sampled.pixel(col, row);
                if (qAlpha(rgb) < 128) continue;

                int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
                QColor color(r, g, b);
                int h, s, l; color.getHsl(&h, &s, &l);
                double sat = s / 255.0, lig = l / 255.0;

                // 问题1：过滤阈值过于激进 (对标 Eagle 基础过滤)
                if (lig > 0.94 && sat < 0.08) continue; // 极白过滤
                if (lig < 0.06) continue;               // 极黑过滤

                // 核心人眼感知权重计算
                double perceptionWeight = 1.0;
                if (sat > 0.08) {
                    // 彩色像素权重：饱和度越高、亮度越处于中性(0.5)的色彩，视觉权重越大
                    double base = sat * (1.0 - std::abs(lig - 0.5) * 2.0);
                    perceptionWeight = 1.0 + 8.0 * base * base;
                } else {
                    // 问题2：无彩色降权幅度过猛，从 0.15 改为 0.4
                    perceptionWeight = 0.4; // 原来是 0.15，过度压制导致少量彩色被淹没
                }

                // 4-bit 量化分组 (4096 桶)
                QRgb rgbKey = qRgb(r & 0xF0, g & 0xF0, b & 0xF0);
                auto& stat = bucketStats[rgbKey];
                stat.rSum += r; stat.gSum += g; stat.bSum += b;
                stat.weightedCount += perceptionWeight;
                stat.absoluteCount++;
                totalWeightedPixels += perceptionWeight;
            }
        }
        if (bucketStats.isEmpty()) return {};

        struct FinalBucket { QColor avgColor; double weightedCount; int absoluteCount; };
        QList<FinalBucket> buckets;
        for (auto it = bucketStats.begin(); it != bucketStats.end(); ++it) {
            const auto& s = it.value();
            buckets.append({ QColor((int)(s.rSum / s.absoluteCount), (int)(s.gSum / s.absoluteCount), (int)(s.bSum / s.absoluteCount)), s.weightedCount, s.absoluteCount });
        }

        // 相似色合并
        QList<FinalBucket> merged;
        for (const auto& b : buckets) {
            bool found = false;
            int h1, s1, l1; b.avgColor.getHsl(&h1, &s1, &l1);
            for (auto& m : merged) {
                int h2, s2, l2; m.avgColor.getHsl(&h2, &s2, &l2);
                int dh = std::abs(h1 - h2); if (dh > 180) dh = 360 - dh;
                int ds = std::abs(s1 - s2), dl = std::abs(l1 - l2);
                // 问题3：相似色合并阈值太宽松，收紧亮度阈值从 20 改为 12
                if (dh < 18 && ds < 20 && dl < 12) {
                    double totalWeight = m.weightedCount + b.weightedCount;
                    int totalAbsolute = m.absoluteCount + b.absoluteCount;
                    m.avgColor = QColor((m.avgColor.red()*m.weightedCount + b.avgColor.red()*b.weightedCount)/totalWeight, (m.avgColor.green()*m.weightedCount + b.avgColor.green()*b.weightedCount)/totalWeight, (m.avgColor.blue()*m.weightedCount + b.avgColor.blue()*b.weightedCount)/totalWeight);
                    m.weightedCount = totalWeight; m.absoluteCount = totalAbsolute;
                    found = true; break;
                }
            }
            if (!found) merged.append(b);
        }

        // 根据感知加权数值降序排序
        std::sort(merged.begin(), merged.end(), [](const FinalBucket& a, const FinalBucket& b) {
            return a.weightedCount > b.weightedCount;
        });

        QVector<QPair<QColor, float>> result;
        for (int i = 0; i < (int)merged.size(); ++i) {
            float ratio = (float)merged[i].weightedCount / totalWeightedPixels;
            // 问题4：最终过滤阈值误杀低占比特征色，从 0.005f 改为 0.002f
            if (ratio < 0.002f) continue;
            result.append({ merged[i].avgColor, ratio });
            if (result.size() >= 10) break;
        }
        return result;
    }

    /**
     * @brief 从图像中提取主色调 (向后兼容封装版)
     */
    static inline QColor extractDominantColor(const QString& targetFile) {
        auto palette = extractPalette(targetFile);
        return palette.isEmpty() ? QColor() : palette.first().first;
    }

public:
    static QImage getShellThumbnail(const QString& path, int size) {
        // 2026-06-xx 物理重构：引入磁盘缓存机制，消除“失忆症”
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString cacheDir = QDir(appData).filePath("thumbs/");
        QDir().mkpath(cacheDir);

        QFileInfo fi(path);
        // 2026-06-xx 物理修复：在 hashKey 中加入 v14 标识，强制失效旧缓存
        QString hashKey = QString("%1_%2_%3_%4_v14").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
        QString safeName = QString::number(qHash(hashKey), 16) + ".png";
        QString cachePath = cacheDir + safeName;

        if (QFile::exists(cachePath)) {
            QImage img;
            if (img.load(cachePath)) return img;
        }

#ifdef Q_OS_WIN
        PIDLIST_ABSOLUTE pidl = nullptr;
        HRESULT hr = SHParseDisplayName(path.toStdWString().c_str(), nullptr, &pidl, 0, nullptr);
        if (FAILED(hr)) return QImage();
        IShellItem* pItem = nullptr;
        hr = SHCreateItemFromIDList(pidl, IID_IShellItem, (void**)&pItem);
        ILFree(pidl);
        if (SUCCEEDED(hr)) {
            IShellItemImageFactory* pFactory = nullptr;
            hr = pItem->QueryInterface(IID_IShellItemImageFactory, (void**)&pFactory);
            if (SUCCEEDED(hr)) {
                SIZE nativeSize = { size, size };
                HBITMAP hBitmap = nullptr;
                hr = pFactory->GetImage(nativeSize, SIIGBF_THUMBNAILONLY | SIIGBF_RESIZETOFIT, &hBitmap);
                if (SUCCEEDED(hr) && hBitmap) {
                    BITMAP bmpInfo;
                    GetObject(hBitmap, sizeof(bmpInfo), &bmpInfo);
                    int w = bmpInfo.bmWidth;
                    int h = std::abs(bmpInfo.bmHeight);

                    BITMAPINFOHEADER bi = {};
                    bi.biSize        = sizeof(BITMAPINFOHEADER);
                    bi.biWidth       = w;
                    bi.biHeight      = -h;   // 负值 = top-down，方向永远正确
                    bi.biPlanes      = 1;
                    bi.biBitCount    = 32;
                    bi.biCompression = BI_RGB;

                    QByteArray pixels(w * h * 4, 0);
                    HDC hdc = GetDC(nullptr);
                    GetDIBits(hdc, hBitmap, 0, h, pixels.data(),
                              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
                    ReleaseDC(nullptr, hdc);

                    // Windows 返回 BGRA，Qt 需要 RGBA，交换 R/B 通道
                    uint8_t* p = reinterpret_cast<uint8_t*>(pixels.data());
                    for (int i = 0; i < w * h; ++i) {
                        std::swap(p[i * 4 + 0], p[i * 4 + 2]);
                    }

                    QImage img(p, w, h, w * 4, QImage::Format_RGBA8888);
                    img = img.copy(); // 确保数据所有权
                    
                    // 异步存入磁盘缓存
                    (void)QtConcurrent::run([img, cachePath]() {
                        img.save(cachePath, "PNG");
                    });

                    DeleteObject(hBitmap);
                    pFactory->Release();
                    pItem->Release();
                    return img;
                }
                pFactory->Release();
            }
            pItem->Release();
        }
#else
        Q_UNUSED(path); Q_UNUSED(size);
#endif
        return QImage();
    }
};

} // namespace ArcMeta
