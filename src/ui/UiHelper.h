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
     * @brief 从图像中提取调色盘 (5 色占比版)
     */
    static QVector<QPair<QColor, float>> extractPalette(const QString& targetFile, int topN = 5) {
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
        QMap<QRgb, int> freqMap;
        int totalValidPixels = 0;

        for (int row = 0; row < sampled.height(); ++row) {
            for (int col = 0; col < sampled.width(); ++col) {
                QRgb rgb = sampled.pixel(col, row);
                // 2. Alpha 过滤
                if (qAlpha(rgb) < 128) continue;

                // 3. 量化：使用 3-bit (& 0xE0) 减少碎片化并保持区分度
                QRgb rgbKey = qRgb(qRed(rgb) & 0xE0, qGreen(rgb) & 0xE0, qBlue(rgb) & 0xE0);
                freqMap[rgbKey]++;
                totalValidPixels++;
            }
        }

        if (freqMap.isEmpty()) return {};

        // 4. 排序频率桶
        QList<QPair<QRgb, int>> sortedBuckets;
        for (auto it = freqMap.begin(); it != freqMap.end(); ++it) {
            sortedBuckets.append({it.key(), it.value()});
        }
        std::sort(sortedBuckets.begin(), sortedBuckets.end(), [](const QPair<QRgb, int>& a, const QPair<QRgb, int>& b) {
            return a.second > b.second;
        });

        // 5. 合并相似颜色桶 (曼哈顿距离 < 32)
        QList<QPair<QRgb, int>> mergedBuckets;
        for (const auto& bucket : sortedBuckets) {
            bool merged = false;
            for (auto& target : mergedBuckets) {
                int dr = std::abs(qRed(bucket.first) - qRed(target.first));
                int dg = std::abs(qGreen(bucket.first) - qGreen(target.first));
                int db = std::abs(qBlue(bucket.first) - qBlue(target.first));
                if (dr + dg + db < 32) {
                    target.second += bucket.second;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                mergedBuckets.append(bucket);
            }
        }

        // 6. 再次排序并截取 Top N
        std::sort(mergedBuckets.begin(), mergedBuckets.end(), [](const QPair<QRgb, int>& a, const QPair<QRgb, int>& b) {
            return a.second > b.second;
        });

        QVector<QPair<QColor, float>> result;
        int count = qMin((int)mergedBuckets.size(), topN);
        for (int i = 0; i < count; ++i) {
            float ratio = (float)mergedBuckets[i].second / totalValidPixels;
            result.append({QColor(mergedBuckets[i].first), ratio});
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
                    if (forceMirror) img = img.flipped(Qt::Vertical);
                    QPixmap pix = QPixmap::fromImage(img);
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
