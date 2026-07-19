#include "SvgIconRenderer.h"
#include "SvgIcons.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QBuffer>
#include <QDir>
#include <QMutexLocker>

namespace ArcMeta {

QMap<QString, QPixmap>& SvgIconRenderer::iconPixmapCache() {
    static QMap<QString, QPixmap> cache;
    return cache;
}

QMutex& SvgIconRenderer::iconMutex() {
    static QMutex mutex;
    return mutex;
}

QPixmap SvgIconRenderer::renderIcon(const QString& key, const QSize& size, const QColor& color) {
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

QString SvgIconRenderer::getSvgDataUrl(const QString& key, const QColor& color) {
    QPixmap pix = renderIcon(key, QSize(20, 20), color);
    if (pix.isNull()) return QString();

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    pix.save(&buffer, "PNG");
    return QString("data:image/png;base64,%1").arg(QString(ba.toBase64()));
}

QIcon SvgIconRenderer::getIcon(const QString& key, const QColor& color, int size) {
    QIcon icon;
    QPixmap pix = getPixmap(key, QSize(size, size), color);
    if (!pix.isNull()) icon.addPixmap(pix);
    return icon;
}

QPixmap SvgIconRenderer::getPixmap(const QString& key, const QSize& size, const QColor& color) {
    QString cKey = QString("%1_%2_%3_%4").arg(key).arg(size.width()).arg(size.height()).arg(color.rgba());

    {
        QMutexLocker locker(&iconMutex());
        if (iconPixmapCache().contains(cKey)) return iconPixmapCache()[cKey];
    }

    QPixmap rendered = renderIcon(key, size, color);
    if (rendered.isNull()) return rendered;

    QMutexLocker locker(&iconMutex());
    iconPixmapCache().insert(cKey, rendered);
    return rendered;
}

QString SvgIconRenderer::getSvgTempFilePath(const QString& key, const QColor& color) {
    QPixmap pix = renderIcon(key, QSize(20, 20), color);
    if (pix.isNull()) return QString();

    QString tmpPath = QDir::temp().filePath(
        QString("arcmeta_%1_%2_v3.png").arg(key).arg(color.name().mid(1))
    );
    pix.save(tmpPath, "PNG");
    return QDir::fromNativeSeparators(tmpPath);
}

void SvgIconRenderer::applyMenuStyle(QWidget* menu) {
    if (!menu) return;
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setWindowFlag(Qt::FramelessWindowHint);

    QString arrowPath = getSvgTempFilePath("menu_triangle", QColor("#CCCCCC"));

    menu->setStyleSheet(QString(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; border-radius: 8px; }"
        "QMenu::item { padding: 6px 25px 6px 10px; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3E3E42; color: white; }"
        "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }"
        "QMenu::right-arrow { "
        "  image: url(%1); "
        "  subcontrol-origin: padding; "
        "  subcontrol-position: center right; "
        "  right: 8px; "
        "}"
    ).arg(arrowPath));
}

} // namespace ArcMeta
