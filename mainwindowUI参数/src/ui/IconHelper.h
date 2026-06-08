#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QIcon>
#include <QMenu>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include "SvgIcons.h"

class IconHelper {
public:
    static QIcon getIcon(const QString& name, const QString& color = "#cccccc", int size = 64) {
        if (!SvgIcons::icons.contains(name)) return QIcon();

        QString svgData = SvgIcons::icons[name];
        svgData.replace("currentColor", color);
        // 如果 svg 中没有 currentColor，强制替换所有可能的 stroke/fill 颜色（简易实现）
        // 这里假设 SVG 字符串格式标准，仅替换 stroke="currentColor" 或 fill="currentColor"
        // 实际上 Python 版是直接全量 replace "currentColor"

        QByteArray bytes = svgData.toUtf8();
        QSvgRenderer renderer(bytes);
        
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        
        QIcon icon;
        icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
        icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
        icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
        return icon;
    }

    // 统一设置 QMenu 样式,移除系统原生直角阴影
    static void setupMenu(QMenu* menu) {
        if (!menu) return;
        // 移除系统原生阴影,使用自定义圆角
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }
};

#endif // ICONHELPER_H
