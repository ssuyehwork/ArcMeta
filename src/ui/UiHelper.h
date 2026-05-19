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
#include <QSettings>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <QSet>
#include <QCoreApplication>
#include <QWidget>
#include <QProcess>
#include <QUuid>
#include <QDir>
#include <QFile>
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
        if (colorName == "yellow" || colorName == "黄") return QColor("#FAC775");
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

    static bool isGraphicsFile(const QString& ext) {
        static const QStringList graphicsExts = {"png", "jpg", "jpeg", "bmp", "gif", "webp", "ico", "tiff", "tif", "psd", "psb", "ai", "eps", "pdf", "svg", "cdr"};
        return graphicsExts.contains(ext.toLower());
    }

    static QIcon getIcon(const QString& key, const QColor& color, int size = 18) {
        QIcon icon;
        QPixmap pix = getPixmap(key, QSize(size, size), color);
        if (!pix.isNull()) icon.addPixmap(pix);
        return icon;
    }

    static QIcon getFileIcon(const QString& filePath, int size = 18, const QColor& overrideColor = QColor()) {
        QFileInfo info(filePath);
        QString ext = info.suffix().toLower();
        QString iconKey = "file";
        QColor baseColor("#aaaaaa");

        if (info.isDir()) {
            iconKey = "folder_filled";
            baseColor = QColor("#3498db");
        } else {
            if (isGraphicsFile(ext)) { iconKey = "image"; baseColor = QColor("#EF9F27"); }
            else if (ext == "pdf") { iconKey = "file_pdf"; baseColor = QColor("#e74c3c"); }
            else if (ext == "doc" || ext == "docx") { iconKey = "file_word"; baseColor = QColor("#3498db"); }
            else if (ext == "xls" || ext == "xlsx" || ext == "csv") { iconKey = "table"; baseColor = QColor("#2ecc71"); }
            else if (ext == "ppt" || ext == "pptx") { iconKey = "file_ppt"; baseColor = QColor("#EF9F27"); }
            else if (QStringList({"cpp", "h", "py", "js", "ts", "html", "css", "json", "xml", "md"}).contains(ext)) { iconKey = "code"; baseColor = QColor("#3498db"); }
            else if (QStringList({"zip", "rar", "7z", "tar", "gz"}).contains(ext)) { iconKey = "archive"; baseColor = QColor("#f1c40f"); }
            else if (QStringList({"exe", "msi", "bat", "sh"}).contains(ext)) { iconKey = "file_executable"; baseColor = QColor("#E81123"); }
            else if (QStringList({"mp4", "mkv", "avi", "mov"}).contains(ext)) { iconKey = "video"; baseColor = QColor("#9b59b6"); }
            else if (QStringList({"mp3", "wav", "flac", "ogg"}).contains(ext)) { iconKey = "music"; baseColor = QColor("#e91e63"); }
        }

        QColor finalColor = overrideColor.isValid() ? overrideColor : baseColor;
        return getIcon(iconKey, finalColor, size);
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

        QSettings settings("ArcMeta团队", "ArcMeta");
        QString settingKey = QString("ExtensionColors/%1").arg(upperExt);
        if (settings.contains(settingKey)) {
            QColor color = settings.value(settingKey).value<QColor>();
            s_cache[upperExt] = color;
            return color;
        }

        size_t hash = qHash(upperExt);
        int hue = static_cast<int>(hash % 360);
        QColor color = QColor::fromHsl(hue, 160, 110, 200); 
        s_cache[upperExt] = color;
        settings.setValue(settingKey, color);
        return color;
    }

    /**
     * @brief 对颜色进行 3-bit 量化，确保存储与搜索的一致性
     */
    static inline QColor quantizeColor(const QColor& color) {
        if (!color.isValid()) return color;
        return QColor(color.red() & 0xE0, color.green() & 0xE0, color.blue() & 0xE0);
    }


    /**
     * @brief 从图像中提取调色盘 (变长色板版)
     */
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile) {
        QFileInfo fileInfo(targetFile);
        QString suffix = fileInfo.suffix().toLower();
        QImage targetImg;
        QString temporaryPng;

        if (suffix == "psd" || suffix == "ai" || suffix == "eps") {
            temporaryPng = convertDesignFileToPng(targetFile);
            if (!temporaryPng.isEmpty()) {
                targetImg.load(temporaryPng);
                QFile::remove(temporaryPng);
            }
        } else {
            targetImg.load(targetFile);
        }

        if (targetImg.isNull()) return {};

        // 1. 采样：使用 128x128 提高颜色覆盖度
        QImage sampled = targetImg.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        struct BucketInfo { 
            long long rSum = 0, gSum = 0, bSum = 0; 
            int count = 0; 
        };
        QMap<QRgb, BucketInfo> bucketStats;
        int totalValidPixels = 0;

        for (int row = 0; row < sampled.height(); ++row) {
            for (int col = 0; col < sampled.width(); ++col) {
                QRgb rgb = sampled.pixel(col, row);
                if (qAlpha(rgb) < 128) continue;

                // 2. 量化分组：使用 3-bit 建立桶
                QRgb rgbKey = qRgb(qRed(rgb) & 0xE0, qGreen(rgb) & 0xE0, qBlue(rgb) & 0xE0);
                auto& stat = bucketStats[rgbKey];
                stat.rSum += qRed(rgb);
                stat.gSum += qGreen(rgb);
                stat.bSum += qBlue(rgb);
                stat.count++;
                totalValidPixels++;
            }
        }

        if (bucketStats.isEmpty()) return {};

        // 3. 计算桶的平均真色并初步排序
        struct FinalBucket { QColor avgColor; int count; };
        QList<FinalBucket> buckets;
        for (auto it = bucketStats.begin(); it != bucketStats.end(); ++it) {
            const auto& s = it.value();
            buckets.append({ QColor((int)(s.rSum / s.count), (int)(s.gSum / s.count), (int)(s.bSum / s.count)), s.count });
        }
        std::sort(buckets.begin(), buckets.end(), [](const FinalBucket& a, const FinalBucket& b) {
            return a.count > b.count;
        });

        // 4. 相似合并 (HSL 空间比对：deltaH < 20, deltaS < 25, deltaL < 20)
        QList<FinalBucket> merged;
        for (const auto& b : buckets) {
            bool found = false;
            int h1, s1, l1; b.avgColor.getHsl(&h1, &s1, &l1);
            
            for (auto& m : merged) {
                int h2, s2, l2; m.avgColor.getHsl(&h2, &s2, &l2);
                
                int dh = std::abs(h1 - h2);
                if (dh > 180) dh = 360 - dh; // 色相环循环处理
                int ds = std::abs(s1 - s2);
                int dl = std::abs(l1 - l2);

                if (dh < 20 && ds < 25 && dl < 20) {
                    // 重新计算加权平均色，确保最终 HEX 真值不偏离物理重心
                    int total = m.count + b.count;
                    int nr = (m.avgColor.red() * m.count + b.avgColor.red() * b.count) / total;
                    int ng = (m.avgColor.green() * m.count + b.avgColor.green() * b.count) / total;
                    int nb = (m.avgColor.blue() * m.count + b.avgColor.blue() * b.count) / total;
                    m.avgColor = QColor(nr, ng, nb);
                    m.count = total;
                    found = true;
                    break;
                }
            }
            if (!found) merged.append(b);
        }

        // 5. 再次排序并返回全量色板
        std::sort(merged.begin(), merged.end(), [](const FinalBucket& a, const FinalBucket& b) {
            return a.count > b.count;
        });

        QVector<QPair<QColor, float>> result;
        for (int i = 0; i < (int)merged.size(); ++i) {
            float ratio = (float)merged[i].count / totalValidPixels;
            // 物理过滤：占比不足 1% 的杂色直接丢弃，对标 Eagle 密度
            if (ratio < 0.01f) continue;
            result.append({ merged[i].avgColor, ratio });
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

private:
    static inline QString convertDesignFileToPng(const QString& srcPath) {
        QString workDir = QCoreApplication::applicationDirPath() + "/Cache/tmp";
        QDir().mkpath(workDir);
        QString dstPath = workDir + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        QString ext = QFileInfo(srcPath).suffix().toLower();

        QProcess converter;
        QString cmd;
        QStringList params;

        if (ext == "psd") {
            cmd = "magick";
            params << srcPath + "[0]" << "-flatten" << dstPath;
        } else if (ext == "ai" || ext == "eps") {
            cmd = "gs";
            params << "-dNOPAUSE" << "-dBATCH" << "-dSAFER" << "-sDEVICE=png16m" << "-r72" << "-dFirstPage=1" << "-dLastPage=1" 
                   << QString("-sOutputFile=%1").arg(dstPath) << srcPath;
        }

        converter.start(cmd, params);
        if (converter.waitForFinished(15000)) {
            if (QFile::exists(dstPath)) return dstPath;
        } else {
            converter.kill();
        }
        return "";
    }

public:
    static QPixmap getShellThumbnail(const QString& path, int size, bool forceMirror = false) {
        // 2026-06-xx 物理重构：引入磁盘缓存机制，消除“失忆症”
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString cacheDir = QDir(appData).filePath("thumbs/");
        QDir().mkpath(cacheDir);

        QFileInfo fi(path);
        QString hashKey = QString("%1_%2_%3_%4").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
        QString safeName = QString::number(qHash(hashKey), 16) + ".png";
        QString cachePath = cacheDir + safeName;

        if (QFile::exists(cachePath)) {
            QPixmap pix;
            if (pix.load(cachePath)) return pix;
        }

#ifdef Q_OS_WIN
        PIDLIST_ABSOLUTE pidl = nullptr;
        HRESULT hr = SHParseDisplayName(path.toStdWString().c_str(), nullptr, &pidl, 0, nullptr);
        if (FAILED(hr)) return QPixmap();
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
                    QImage img = QImage::fromHBITMAP(hBitmap);
                    // 2026-06-xx 物理修复：Windows Shell 缩略图通常为 Bottom-Up 存储，
                    // QImage::fromHBITMAP 在某些 DIB 格式下不会自动翻转，导致显示颠倒。
                    // 强制进行垂直翻转 (flipped) 以符合人类视觉并消除弃用警告。
                    img = img.flipped(Qt::Vertical); 
                    if (forceMirror) img = img.flipped(Qt::Vertical); // 再次翻转即还原
                    QPixmap pix = QPixmap::fromImage(img);

                    // 异步存入磁盘缓存
                    (void)QtConcurrent::run([img, cachePath]() {
                        img.save(cachePath, "PNG");
                    });

                    DeleteObject(hBitmap);
                    pFactory->Release();
                    pItem->Release();
                    return pix;
                }
                pFactory->Release();
            }
            pItem->Release();
        }
#else
        Q_UNUSED(path); Q_UNUSED(size); Q_UNUSED(forceMirror);
#endif
        return QPixmap();
    }
};

} // namespace ArcMeta
