#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once

#include <QIcon>
#include <QString>
#include <QColor>
#include <QPixmap>
#include <QSize>
#include <QWidget>
#include <QImage>
#include <QVector>
#include <QPair>
#include <QDebug>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QMap>

#include "SvgIconRenderer.h"
#include "WindowsShellThumbnailProvider.h"
#include "MediaColorExtractor.h"

namespace ArcMeta {

/**
 * @brief UI 辅助兼容及转发层 (完全解耦重构版)
 */
class UiHelper {
public:
    static inline void initializeHotIcons() {
        qDebug() << "[UiHelper] 图标系统已启用懒加载模式";
        WindowsShellThumbnailProvider::instance();
    }

    static inline QColor parseColorName(const QString& colorName) {
        if (colorName.isEmpty()) return QColor();
        
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

    static inline QPixmap renderIcon(const QString& key, const QSize& size, const QColor& color) {
        return SvgIconRenderer::renderIcon(key, size, color);
    }

    static inline QString getSvgDataUrl(const QString& key, const QColor& color = QColor("#3498db")) {
        return SvgIconRenderer::getSvgDataUrl(key, color);
    }

    static inline QString getSvgTempFilePath(const QString& key, const QColor& color) {
        return SvgIconRenderer::getSvgTempFilePath(key, color);
    }

    static inline bool isGraphicsFile(const QString& ext) {
        return MediaColorExtractor::isGraphicsFile(ext);
    }

    static inline bool isStandardImage(const QString& ext) {
        return MediaColorExtractor::isStandardImage(ext);
    }

    static inline QIcon getIcon(const QString& key, const QColor& color, int size = 18) {
        return SvgIconRenderer::getIcon(key, color, size);
    }

    static inline QIcon getFileIcon(const QString& filePath, int size = 18, const QColor& overrideColor = QColor()) {
        Q_UNUSED(overrideColor);
        Q_UNUSED(size);

        QFileInfo info(filePath);
        // 1. 如果是目录，正常获取文件夹图标
        if (info.isDir()) {
            QFileIconProvider provider;
            if (info.isRoot()) return provider.icon(info);
            return provider.icon(QFileIconProvider::Folder);
        }

        // 2. 🚨 物理对齐 FERREX-META 秘密 2：针对文件，使用虚拟扩展名 "dummy.ext" 提取纯净图标！
        // 彻底杜绝 Windows 传回带有深色圆角方块的粗糙系统图标！
        QString ext = info.suffix().toLower();
        if (ext.isEmpty()) ext = "unknown";

        static QMap<QString, QIcon> s_cleanIconCache;
        if (s_cleanIconCache.contains(ext)) {
            return s_cleanIconCache[ext];
        }

        QFileIconProvider provider;
        // 给 QFileIconProvider 传虚拟文件名 dummy.ext，拿到 100% 透明背景的纯净矢量图标
        QIcon cleanIcon = provider.icon(QFileInfo("dummy." + ext));
        if (cleanIcon.isNull()) {
            cleanIcon = provider.icon(QFileIconProvider::File);
        }

        s_cleanIconCache[ext] = cleanIcon;
        return cleanIcon;
    }

    static inline QPixmap getPixmap(const QString& key, const QSize& size, const QColor& color) {
        return SvgIconRenderer::getPixmap(key, size, color);
    }

    static inline void applyMenuStyle(QWidget* menu) {
        SvgIconRenderer::applyMenuStyle(menu);
    }

    static inline QColor getExtensionColor(const QString& ext) {
        return MediaColorExtractor::getExtensionColor(ext);
    }

    static inline QColor quantizeColor(const QColor& color) {
        return MediaColorExtractor::quantizeColor(color);
    }

    static inline double calculateDeltaE(const QColor& c1, const QColor& c2) {
        return MediaColorExtractor::calculateDeltaE(c1, c2);
    }

    static inline QImage getImageForAnalysis(const QString& path, int size = 256) {
        return MediaColorExtractor::getImageForAnalysis(path, size);
    }

    static inline QVector<QPair<QColor, float>> extractPalette(const QString& targetFile) {
        return MediaColorExtractor::extractPalette(targetFile);
    }

    static inline QColor extractDominantColor(const QString& targetFile) {
        return MediaColorExtractor::extractDominantColor(targetFile);
    }

    static inline QImage getShellThumbnail(const QString& path, int size) {
        return WindowsShellThumbnailProvider::getShellThumbnail(path, size);
    }
};

} // namespace ArcMeta
