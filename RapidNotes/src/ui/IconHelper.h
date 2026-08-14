#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QIcon>
#include <QMenu>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QMutex>
#include <QMutexLocker>
#include "SvgIcons.h"

class IconHelper {
private:
    // 2026-04-11 按照用户要求：增加全局图标缓存，避免重复渲染 SVG 造成的 CPU 浪费
    inline static QMap<QString, QIcon> s_iconCache;
    inline static QMutex s_cacheMutex;

public:
    static QIcon getIcon(const QString& name, const QString& color = "#cccccc", int size = 64) {
        // 构造缓存键：图标名 + 颜色 + 尺寸
        QString key = QString("%1_%2_%3").arg(name, color).arg(size);

        // 使用互斥锁确保线程安全
        QMutexLocker locker(&s_cacheMutex);
        if (s_iconCache.contains(key)) {
            return s_iconCache[key];
        }

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

        // 存入缓存
        s_iconCache[key] = icon;
        
        return icon;
    }

    // 清理缓存接口
    static void clearCache() {
        QMutexLocker locker(&s_cacheMutex);
        s_iconCache.clear();
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
