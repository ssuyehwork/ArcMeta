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
        
        return QColor();
    }

    static QString mapToPredefinedColor(const QColor& color) {
        if (!color.isValid()) return "";
        QList<QPair<QString, QColor>> targets = {
            {"red", QColor("#E24B4A")},
            {"orange", QColor("#EF9F27")},
            {"yellow", QColor("#FAC775")},
            {"green", QColor("#639922")},
            {"cyan", QColor("#1D9E75")},
            {"blue", QColor("#378ADD")},
            {"purple", QColor("#7F77DD")},
            {"gray", QColor("#5F5E5A")}
        };
        QString closestName = "gray";
        long long minDistanceSq = 1e18;
        for (const auto& target : targets) {
            long r = color.red() - target.second.red();
            long g = color.green() - target.second.green();
            long b = color.blue() - target.second.blue();
            long rmean_avg = (color.red() + target.second.red()) / 2;
            long long distSq = (((512 + rmean_avg) * r * r) >> 8) + 4 * g * g + (((767 - rmean_avg) * b * b) >> 8);
            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                closestName = target.first;
            }
        }
        return closestName;
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
     * @brief 从图像中提取主色调 (2026-05-16 健壮版)
     */
    static inline QColor extractDominantColor(const QString& targetFile) {
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

        if (targetImg.isNull()) return QColor();

        // 缩放并进行频率分析
        QImage sampled = targetImg.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QMap<QRgb, int> freqMap;
        for (int row = 0; row < sampled.height(); ++row) {
            for (int col = 0; col < sampled.width(); ++col) {
                QColor pixCol = sampled.pixelColor(col, row);
                // 排除无效背景
                if (pixCol.saturation() < 30 || pixCol.value() > 240 || pixCol.value() < 20) continue;
                
                // 量化聚合
                QRgb rgbKey = qRgb(pixCol.red() & 0xF0, pixCol.green() & 0xF0, pixCol.blue() & 0xF0);
                freqMap[rgbKey]++;
            }
        }

        if (freqMap.isEmpty()) {
            return targetImg.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
        }

        QRgb winnerRgb = 0;
        int maxFreq = 0;
        for (auto it = freqMap.begin(); it != freqMap.end(); ++it) {
            if (it.value() > maxFreq) {
                maxFreq = it.value();
                winnerRgb = it.key();
            }
        }
        return QColor(winnerRgb);
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
