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
     * @brief 从图像中提取调色盘 (工业级优化版)
     * 2026-06-xx 架构重构：彻底弃用外部工具链 (ImageMagick/Ghostscript)，全面转向原生 Shell 引擎
     */
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile) {
        // 优先从系统缩略图引擎获取数据，支持 PSD, AI, EPS, PDF 等专业格式 (前提是系统有预览插件)
        QImage targetImg = getShellThumbnail(targetFile, 128);

        // 回退：针对普通图片或无插件环境，直接通过 Qt 加载
        if (targetImg.isNull()) {
            targetImg.load(targetFile);
        }

        // 核心防御：加载图像后必须立即进行空值检查，防止后续像素处理逻辑崩溃
        if (targetImg.isNull()) return {};

        // 1. 采样：使用 128x128 采样以保持极高性能和足够的颜色覆盖度
        QImage sampled = targetImg.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        struct BucketInfo { 
            long long rSum = 0, gSum = 0, bSum = 0; 
            double weightedCount = 0.0; // 视觉感知加权统计计数
            int absoluteCount = 0;      // 物理真实像素统计计数
        };
        QMap<QRgb, BucketInfo> bucketStats;
        double totalWeightedPixels = 0.0;

        for (int row = 0; row < sampled.height(); ++row) {
            for (int col = 0; col < sampled.width(); ++col) {
                QRgb rgb = sampled.pixel(col, row);
                if (qAlpha(rgb) < 128) continue; // 过滤高透明度像素

                int r = qRed(rgb);
                int g = qGreen(rgb);
                int b = qBlue(rgb);

                // 计算 HSL 进行人类视觉特征提取判定
                QColor color(r, g, b);
                int h, s, l;
                color.getHsl(&h, &s, &l);

                double sat = s / 255.0; // 0.0 ~ 1.0
                double lig = l / 255.0; // 0.0 ~ 1.0

                // 2. 主动过滤无用噪色与背景色
                // 极白背景过滤：极亮(L > 94%) 且 极淡(S < 8%) 的背景白色，予以直接过滤，腾出色彩席位
                if (lig > 0.94 && sat < 0.08) {
                    continue;
                }
                // 极黑边缘线过滤：极暗(L < 6%)，剔除线条阴影干扰
                if (lig < 0.06) {
                    continue;
                }

                // 3. 核心人眼感知权重计算 (高鲜艳特征倾向)
                double perceptionWeight = 1.0;
                if (sat > 0.08) {
                    // 彩色像素权重：饱和度越高、亮度越处于中性(0.5)的色彩，视觉权重越大 (最高放大 9 倍)
                    double base = sat * (1.0 - std::abs(lig - 0.5) * 2.0);
                    perceptionWeight = 1.0 + 8.0 * base * base;
                } else {
                    // 纯灰色/无彩色大幅度降权，避免无用淡灰/暗灰色把调色盘挤满
                    perceptionWeight = 0.15;
                }

                // 4. 升级为 4-bit 掩码精细分组量化（空间细分为 4096 桶，防止低位截断污染）
                QRgb rgbKey = qRgb(r & 0xF0, g & 0xF0, b & 0xF0);
                auto& stat = bucketStats[rgbKey];
                stat.rSum += r;
                stat.gSum += g;
                stat.bSum += b;
                stat.weightedCount += perceptionWeight;
                stat.absoluteCount++;
                totalWeightedPixels += perceptionWeight;
            }
        }

        if (bucketStats.isEmpty()) return {};

        // 5. 过滤掉极低频噪点像素桶（物理绝对数量阈值：占总采样数的 0.05%）
        int minAbsoluteCount = std::max(5, (int)(sampled.width() * sampled.height() * 0.0005));
        
        struct FinalBucket { 
            QColor avgColor; 
            double weightedCount; 
            int absoluteCount; 
        };
        QList<FinalBucket> buckets;
        for (auto it = bucketStats.begin(); it != bucketStats.end(); ++it) {
            const auto& s = it.value();
            if (s.absoluteCount < minAbsoluteCount) continue; // 过滤偶发噪点
            
            buckets.append({ 
                QColor((int)(s.rSum / s.absoluteCount), (int)(s.gSum / s.absoluteCount), (int)(s.bSum / s.absoluteCount)), 
                s.weightedCount, 
                s.absoluteCount 
            });
        }

        // 保底处理：如果全部桶被绝对阈值误杀，则不设卡重新载入
        if (buckets.isEmpty()) {
            for (auto it = bucketStats.begin(); it != bucketStats.end(); ++it) {
                const auto& s = it.value();
                buckets.append({ 
                    QColor((int)(s.rSum / s.absoluteCount), (int)(s.gSum / s.absoluteCount), (int)(s.bSum / s.absoluteCount)), 
                    s.weightedCount, 
                    s.absoluteCount 
                });
            }
        }

        // 初步按照感知加权排序
        std::sort(buckets.begin(), buckets.end(), [](const FinalBucket& a, const FinalBucket& b) {
            return a.weightedCount > b.weightedCount;
        });

        // 6. 相似色彩合并 (HSL空间聚类，且保护高饱和度有彩色)
        QList<FinalBucket> merged;
        for (const auto& b : buckets) {
            bool found = false;
            int h1, s1, l1; b.avgColor.getHsl(&h1, &s1, &l1);
            
            for (auto& m : merged) {
                int h2, s2, l2; m.avgColor.getHsl(&h2, &s2, &l2);
                
                int dh = std::abs(h1 - h2);
                if (dh > 180) dh = 360 - dh; // 环形处理
                int ds = std::abs(s1 - s2);
                int dl = std::abs(l1 - l2);

                // 判定色彩相似度范围
                if (dh < 20 && ds < 25 && dl < 20) {
                    double totalWeight = m.weightedCount + b.weightedCount;
                    int totalAbsolute = m.absoluteCount + b.absoluteCount;

                    // 饱和度保护性融合：为色彩本身更鲜艳的色桶赋予更大的平均色算术比重，避免其被偏灰大桶稀释同化
                    double mColorWeight = m.weightedCount * (1.0 + (s2 / 255.0));
                    double bColorWeight = b.weightedCount * (1.0 + (s1 / 255.0));
                    double colorWeightSum = mColorWeight + bColorWeight;

                    int nr = (int)((m.avgColor.red() * mColorWeight + b.avgColor.red() * bColorWeight) / colorWeightSum);
                    int ng = (int)((m.avgColor.green() * mColorWeight + b.avgColor.green() * bColorWeight) / colorWeightSum);
                    int nb = (int)((m.avgColor.blue() * mColorWeight + b.avgColor.blue() * bColorWeight) / colorWeightSum);

                    m.avgColor = QColor(nr, ng, nb);
                    m.weightedCount = totalWeight;
                    m.absoluteCount = totalAbsolute;
                    found = true;
                    break;
                }
            }
            if (!found) merged.append(b);
        }

        // 再次根据感知加权数值降序排序
        std::sort(merged.begin(), merged.end(), [](const FinalBucket& a, const FinalBucket& b) {
            return a.weightedCount > b.weightedCount;
        });

        // 7. 生成最终高表现力调色盘 (去噪、背景限制与 Eagle 席位对标)
        QVector<QPair<QColor, float>> result;
        int whiteBackgroundCount = 0; // 限制纯白/极淡色背景的名额，最多允许 1 个

        for (int i = 0; i < (int)merged.size(); ++i) {
            float ratio = (float)merged[i].weightedCount / totalWeightedPixels;
            if (ratio < 0.005f) continue; // 过滤极低频感知色

            int h, s, l;
            merged[i].avgColor.getHsl(&h, &s, &l);

            // 背景特征白/极亮色检测：饱和度极低且亮度极高 (如大片空白画布)
            if (l > 225 && s < 20) {
                if (whiteBackgroundCount >= 1) {
                    continue; // 忽略重复的多余亮白背景色块，保留特征彩色的珍贵位置
                }
                whiteBackgroundCount++;
            }

            result.append({ merged[i].avgColor, ratio });
            if (result.size() >= 10) break; // 严格对标 Eagle 的 8 ~ 10 席上限
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
        // 2026-06-xx 物理修复：在 hashKey 中加入 v13 标识，强制失效旧缓存
        QString hashKey = QString("%1_%2_%3_%4_v13").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
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
