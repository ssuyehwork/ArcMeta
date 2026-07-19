#pragma once

#include <QIcon>
#include <QString>
#include <QColor>
#include <QPixmap>
#include <QMap>
#include <QMutex>
#include <QWidget>

namespace ArcMeta {

class SvgIconRenderer {
public:
    static QMap<QString, QPixmap>& iconPixmapCache();
    static QMutex& iconMutex();

    static QPixmap renderIcon(const QString& key, const QSize& size, const QColor& color);
    static QString getSvgDataUrl(const QString& key, const QColor& color = QColor("#3498db"));
    static QIcon getIcon(const QString& key, const QColor& color, int size = 18);
    static QPixmap getPixmap(const QString& key, const QSize& size, const QColor& color);
    static QString getSvgTempFilePath(const QString& key, const QColor& color);
    static void applyMenuStyle(QWidget* menu);
};

} // namespace ArcMeta
