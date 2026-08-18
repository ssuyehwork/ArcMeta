#include "QuickLookWindow.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "MediaColorExtractor.h"
#include "QuickLookMinimap.h"
#include "../util/DiskMediaExtractor.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "StyleLibrary.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QDir>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include "../util/ShellHelper.h"
#include <QFileInfo>
#include <QScreen>
#include <QApplication>
#include <QPainter>
#include <QFile>
#include <QStringDecoder>
#include <QScrollBar>
#include <algorithm>
#include <QSvgRenderer>
#include <QtConcurrent>
#include <QPointer>
#include <QTimer>
#include <QWheelEvent>
#include <QSet>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace QuarkMeta {

// 静态文件分类后缀定义 (音视频格式并入黑名单进行系统大图标降级预览，不直接播放)
static const QSet<QString> UNPREVIEWABLE_EXTS = {
    "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "exe", "dll", "msi", "sys", "iso", "dmg", "pkg", "bin", "lnk",
    "mp4", "m4v", "mov", "avi", "mkv", "wmv", "flv", "webm", "3gp", "mp3", "wav", "wma", "flac", "aac", "ogg", "m4a", "ape"
};

// ==========================================
// QuickLookGraphicsView 实现
// ==========================================

QuickLookGraphicsView::QuickLookGraphicsView(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_scene->addItem(m_pixmapItem);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setStyleSheet("background: transparent; border: none;");
    
    // 美化滚动条，对齐系统考古全局规范：宽度 10px、圆角 3px、背景透明、Handle 颜色对齐 #333333
    horizontalScrollBar()->setStyleSheet(R"(
        QScrollBar:horizontal { height: 10px; background: transparent; }
        QScrollBar::handle:horizontal { background: #333333; border-radius: 3px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { border: none; background: none; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }
    )");
    verticalScrollBar()->setStyleSheet(R"(
        QScrollBar:vertical { width: 10px; background: transparent; }
        QScrollBar::handle:vertical { background: #333333; border-radius: 3px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )");

    // 创建右下角小地图
    m_minimap = new QuickLookMinimap(this);
    
    // 小地图点击/拖拽 ➔ 驱动主视口平移中心点
    connect(m_minimap, &QuickLookMinimap::centerRequested, this, [this](double xRatio, double yRatio) {
        if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) return;
        QRectF totalRect = m_pixmapItem->boundingRect();
        QPointF targetCenter(xRatio * totalRect.width(), yRatio * totalRect.height());
        centerOn(targetCenter);
        updateMinimap();
    });
}

void QuickLookGraphicsView::setPixmap(const QPixmap& pixmap) {
    m_pixmapItem->setPixmap(pixmap);
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    
    if (m_minimap) {
        m_minimap->setPixmap(pixmap);
    }
    
    setZoomOriginal(); // 2026-11-xx：将“原始大小模式（100% 比例）”作为默认
    updateMinimap();   // 计算是否显示小地图
}

void QuickLookGraphicsView::clear() {
    m_pixmapItem->setPixmap(QPixmap());
    m_scene->setSceneRect(QRectF());
    resetTransform();
    m_currentScale = 1.0;
    m_isFitMode = false; // 2026-11-xx：默认模式设定为原始大小模式（false）
    if (m_minimap) m_minimap->clear();
    updateCursor();
}

void QuickLookGraphicsView::fitImage() {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) return;
    
    resetTransform();
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    
    m_currentScale = transform().m11();
    m_isFitMode = true;
    updateCursor();
}

void QuickLookGraphicsView::setZoomOriginal() {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) return;
    
    resetTransform();
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    m_currentScale = 1.0;
    m_isFitMode = false;
    updateCursor();
}

void QuickLookGraphicsView::rotateClockwise() {
    rotate(90);
    updateCursor();
}

void QuickLookGraphicsView::flipHorizontal() {
    scale(-1, 1);
    updateCursor();
}

void QuickLookGraphicsView::wheelEvent(QWheelEvent* event) {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    double factor = 1.15;
    if (event->angleDelta().y() < 0) {
        factor = 1.0 / factor;
    }

    double newScale = m_currentScale * factor;
    if (newScale < 0.1) {
        factor = 0.1 / m_currentScale;
        newScale = 0.1;
    } else if (newScale > 10.0) {
        factor = 10.0 / m_currentScale;
        newScale = 10.0;
    }

    if (qFuzzyCompare(newScale, m_currentScale)) {
        return;
    }

    m_isFitMode = false;
    scale(factor, factor);
    m_currentScale = newScale;
    updateCursor();
    updateMinimap(); // 缩放后刷新小地图
}

void QuickLookGraphicsView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 2026-11-xx：放弃当前双击切换自适应/原始像素功能，双击时直接关闭预览
        QuickLookWindow::instance().closePreview();
        event->accept();
    } else {
        QGraphicsView::mouseDoubleClickEvent(event);
    }
}

void QuickLookGraphicsView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (m_isFitMode) {
        fitImage();
    }
    updateMinimap(); // 调整窗口大小后刷新小地图
}

void QuickLookGraphicsView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        bool exceeds = (m_pixmapItem->boundingRect().width() * m_currentScale > viewport()->width()) ||
                       (m_pixmapItem->boundingRect().height() * m_currentScale > viewport()->height());
        if (exceeds) {
            setCursor(Qt::ClosedHandCursor);
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void QuickLookGraphicsView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    updateCursor();
}

void QuickLookGraphicsView::mouseMoveEvent(QMouseEvent* event) {
    QGraphicsView::mouseMoveEvent(event);
    if (event->buttons() & Qt::LeftButton) {
        updateMinimap(); // 按住抓手拖拽时实时刷新小地图！
    }
}

void QuickLookGraphicsView::updateMinimap() {
    if (!m_minimap || !m_pixmapItem || m_pixmapItem->pixmap().isNull()) {
        if (m_minimap) m_minimap->hide();
        return;
    }

    QRectF totalRect = m_pixmapItem->boundingRect();
    QRectF visibleRect = mapToScene(viewport()->rect()).boundingRect();

    // 判定条件：只有当图片物理尺寸超出了当前视口（视口看的是局部）时才展示小地图
    bool exceedsHorizontal = visibleRect.width() < totalRect.width() * 0.99;
    bool exceedsVertical = visibleRect.height() < totalRect.height() * 0.99;

    if (exceedsHorizontal || exceedsVertical) {
        m_minimap->updateViewportRect(visibleRect, totalRect);
        
        // 精确定位在右下角 (右边距 20px，底边距 20px)
        int mx = viewport()->width() - m_minimap->width() - 20;
        int my = viewport()->height() - m_minimap->height() - 20;
        m_minimap->move(mx, my);
        
        m_minimap->show();
        m_minimap->raise(); // 悬浮在画面上方
    } else {
        m_minimap->hide(); // 完整展示时隐去
    }
}

void QuickLookGraphicsView::updateCursor() {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    bool exceedsHorizontal = m_pixmapItem->boundingRect().width() * m_currentScale > viewport()->width();
    bool exceedsVertical = m_pixmapItem->boundingRect().height() * m_currentScale > viewport()->height();
    
    if (exceedsHorizontal || exceedsVertical) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}


// ==========================================
// QuickLookWindow 实现
// ==========================================

QuickLookWindow& QuickLookWindow::instance() {
    static QuickLookWindow inst;
    return inst;
}

QuickLookWindow::QuickLookWindow() : QWidget(nullptr) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool);
    
    setupUi();
    installEventFilter(this);
}

QuickLookWindow::~QuickLookWindow() {}

void QuickLookWindow::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_container = new QWidget();
    m_container->setObjectName("QLContainer");
    m_container->setStyleSheet(R"(
        #QLContainer {
            background-color: #1E1E1E;
        }
        QLabel { color: #CCC; font-size: 12px; }
        #QLTitle { color: #FF8C00; font-weight: bold; font-size: 14px; }
        QPlainTextEdit {
            background: transparent;
            border: none;
            color: #D4D4D4;
            font-family: 'Consolas', 'Monaco', 'PingFang SC', 'Microsoft YaHei';
            font-size: 13px;
        }
    )");

    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_titleLabel = new QLabel(m_container);
    m_titleLabel->setObjectName("QLTitle");
    m_titleLabel->hide();

    // 图片渲染控件
    m_graphicsView = new QuickLookGraphicsView();
    m_graphicsView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_graphicsView->installEventFilter(this);
    layout->addWidget(m_graphicsView);

    // 文本渲染控件
    m_textEdit = new QPlainTextEdit();
    m_textEdit->setReadOnly(true);
    m_textEdit->hide();
    // 重构 QPlainTextEdit 的垂直与水平滚动条，使其完全满足全局考古标准
    m_textEdit->verticalScrollBar()->setStyleSheet(R"(
        QScrollBar:vertical { width: 10px; background: transparent; }
        QScrollBar::handle:vertical { background: #333333; border-radius: 3px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )");
    m_textEdit->horizontalScrollBar()->setStyleSheet(R"(
        QScrollBar:horizontal { height: 10px; background: transparent; }
        QScrollBar::handle:horizontal { background: #333333; border-radius: 3px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { border: none; background: none; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }
    )");
    m_textEdit->installEventFilter(this);
    m_textEdit->viewport()->installEventFilter(this);
    layout->addWidget(m_textEdit);

    // 状态与信息标签
    m_infoLabel = new QLabel(m_container);
    m_infoLabel->setStyleSheet("color: #777;");
    m_infoLabel->hide();

    rootLayout->addWidget(m_container);
}

void QuickLookWindow::previewFile(const QString& path) {
    preview(path);
}

void QuickLookWindow::preview(const QString& filePath) {
    m_currentPath = filePath;
    QFileInfo fi(filePath);
    m_titleLabel->setText(fi.fileName());
    m_infoLabel->setStyleSheet("color: #777;");
    
    QString ext = fi.suffix().toLower();
    
    if (UiHelper::isGraphicsFile(ext)) {
        renderImage(filePath);
    } else if (UNPREVIEWABLE_EXTS.contains(ext)) {
        // 直接提示不支持，显示其系统图标
        m_graphicsView->hide();
        m_textEdit->hide();
        
        m_graphicsView->clear();
        m_textEdit->clear();
        
        QIcon fileIcon = ShellIconManager::getFileIcon(filePath, 256);
        QPixmap pix = fileIcon.pixmap(256, 256);
        m_graphicsView->setPixmap(pix);
        m_graphicsView->show();
        
        m_infoLabel->setText("该文件类型暂不支持预览");
        m_infoLabel->setStyleSheet("color: #FF8C00; font-weight: bold; font-size: 14px;");
    } else {
        renderText(filePath);
    }

    showFullScreen();
    raise();
    activateWindow();

#ifdef Q_OS_WIN
    // 强制置顶保护
    m_ignoreDeactivate = true;
    QTimer::singleShot(150, this, [this]() {
        m_ignoreDeactivate = false;
    });
    SetWindowPos((HWND)winId(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#endif
}

void QuickLookWindow::closePreview() {
    hide();
}

void QuickLookWindow::renderImage(const QString& path) {
    m_textEdit->hide();
    m_graphicsView->show();
    m_graphicsView->clear();
    m_infoLabel->setText("正在加载预览...");

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

    // 优先读取原始像素的本地格式 (由于 Qt 可能未安装/未部署 WebP 图像解码器插件，WebP 格式应交由系统 Shell 提供高分辨率缩略图预览)
    static const QSet<QString> QT_NATIVE_FORMATS = {"png", "jpg", "jpeg", "bmp", "gif"};

    QPointer<QuickLookWindow> weakThis(this);
    (void)QtConcurrent::run([weakThis, path, ext]() {
        if (!weakThis) return;
        
        QImage img;
        bool isInsideArc = path.contains(".arc", Qt::CaseInsensitive);
        if (ext == "svg") {
            QSvgRenderer renderer(path);
            if (renderer.isValid()) {
                img = QImage(2048, 2048, QImage::Format_ARGB32);
                img.fill(Qt::transparent);
                QPainter painter(&img);
                renderer.render(&painter);
            }
        } else if (ext == "ai" || ext == "eps" || ext == "psd" || ext == "psb") {
            if (isInsideArc) {
                img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 2048);
            } else {
                img = DiskMediaExtractor::getDiskThumbnail(path, 2048);
            }
        } else if (QT_NATIVE_FORMATS.contains(ext)) {
            img.load(path);
        } else {
            if (isInsideArc) {
                img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 2048);
            } else {
                img = DiskMediaExtractor::getDiskThumbnail(path, 2048);
            }
            if (img.isNull()) {
                img = ShellIconManager::getShellThumbnail(path, 4096);
                if (img.isNull()) {
                    img.load(path);
                }
            }
        }

        if (!weakThis) return;
        QMetaObject::invokeMethod(weakThis.data(), [weakThis, img, path]() {
            if (!weakThis || weakThis->m_currentPath != path) return;
            if (!img.isNull()) {
                qint64 totalPixels = static_cast<qint64>(img.width()) * img.height();
                bool isHuge = totalPixels > 50000000LL; // 超过 5000 万像素安全降采样
                
                QPixmap pix;
                if (isHuge) {
                    pix = QPixmap::fromImage(img.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    weakThis->m_infoLabel->setText(QString("超大图像（已应用安全限制）: %1x%2 | %3")
                        .arg(img.width()).arg(img.height()).arg(path));
                } else {
                    pix = QPixmap::fromImage(img);
                    weakThis->m_infoLabel->setText(QString("%1x%2 | %3")
                        .arg(img.width()).arg(img.height()).arg(path));
                }
                pix.setDevicePixelRatio(weakThis->devicePixelRatioF());
                weakThis->m_graphicsView->setPixmap(pix);
            } else {
                weakThis->renderText(path); // 图片加载失败尝试文本模式
            }
        });
    });
}

void QuickLookWindow::renderText(const QString& path) {
    m_graphicsView->hide();
    m_textEdit->show();
    m_textEdit->setPlainText("正在读取文件...");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_textEdit->setPlainText("无法打开文件进行预览。");
        return;
    }

    QByteArray fileData = file.read(128 * 1024);
    file.close();

    bool potentialUtf16 = fileData.startsWith("\xFF\xFE") || fileData.startsWith("\xFE\xFF");
    if (!potentialUtf16 && isBinary(fileData)) {
        m_textEdit->hide();
        m_graphicsView->show();
        m_graphicsView->clear();
        
        QIcon fileIcon = ShellIconManager::getFileIcon(path, 256);
        QPixmap pix = fileIcon.pixmap(256, 256);
        m_graphicsView->setPixmap(pix);
        
        m_infoLabel->setText("二进制文件，无法直接预览文本");
        m_infoLabel->setStyleSheet("color: #FF8C00; font-weight: bold; font-size: 14px;");
        return;
    }

    QString encodingName = detectEncoding(fileData);
    QString text;

    if (encodingName == "UTF-8") {
        text = QString::fromUtf8(fileData);
    } else if (encodingName == "UTF-16LE") {
        text = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(fileData.constData()), fileData.size() / 2);
    } else if (encodingName == "UTF-16BE") {
        auto decoder = QStringDecoder(QStringDecoder::Utf16BE);
        text = decoder(fileData);
    } else {
        text = QString::fromLocal8Bit(fileData);
    }

    m_textEdit->setPlainText(text);
    m_textEdit->verticalScrollBar()->setValue(0);
    m_infoLabel->setText(QString("编码: %1 | 大小: %2 KB | %3").arg(encodingName).arg(QFileInfo(path).size() / 1024.0, 0, 'f', 1).arg(path));
}


bool QuickLookWindow::isBinary(const QByteArray& fileData) {
    if (fileData.isEmpty()) return false;
    int checkLen = std::min<int>(fileData.size(), 1024);
    int continuousNull = 0;
    for (int i = 0; i < checkLen; ++i) {
        if (fileData[i] == '\0') {
            continuousNull++;
            if (continuousNull > 2) return true;
        } else {
            continuousNull = 0;
        }
    }
    return false;
}

QString QuickLookWindow::detectEncoding(const QByteArray& fileData) {
    if (fileData.startsWith("\xEF\xBB\xBF")) return "UTF-8";
    if (fileData.startsWith("\xFF\xFE")) return "UTF-16LE";
    if (fileData.startsWith("\xFE\xFF")) return "UTF-16BE";

    int utf8Count = 0;
    for (int i = 0; i < fileData.size() - 2; ++i) {
        unsigned char c = (unsigned char)fileData[i];
        if (c >= 0xC0 && c <= 0xDF) {
            if ((unsigned char)fileData[i+1] >= 0x80 && (unsigned char)fileData[i+1] <= 0xBF) { utf8Count++; i++; }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if ((unsigned char)fileData[i+1] >= 0x80 && (unsigned char)fileData[i+1] <= 0xBF &&
                (unsigned char)fileData[i+2] >= 0x80 && (unsigned char)fileData[i+2] <= 0xBF) { utf8Count += 2; i += 2; }
        }
    }

    return (utf8Count > 0) ? "UTF-8" : "GBK";
}

void QuickLookWindow::keyPressEvent(QKeyEvent* event) {
    // 支持 Ctrl+W 关闭空格文件预览窗口
    if (event->key() == Qt::Key_W && (event->modifiers() & Qt::ControlModifier)) {
        closePreview();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape) {
        closePreview();
        return;
    }
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Left) {
        emit prevRequested();
        return;
    }
    if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Right) {
        emit nextRequested();
        return;
    }

    // 评分标记：1-5 键
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5 && !(event->modifiers() & Qt::AltModifier)) {
        int rating = event->key() - Qt::Key_0;
        emit ratingRequested(rating);
        return;
    }

    // 颜色标记：Alt + 1-9
    if (event->modifiers() & Qt::AltModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        QString color;
        switch (event->key()) {
            case Qt::Key_1: color = "red"; break;
            case Qt::Key_2: color = "orange"; break;
            case Qt::Key_3: color = "yellow"; break;
            case Qt::Key_4: color = "green"; break;
            case Qt::Key_5: color = "cyan"; break;
            case Qt::Key_6: color = "blue"; break;
            case Qt::Key_7: color = "purple"; break;
            case Qt::Key_8: color = "gray"; break;
            case Qt::Key_9: color = ""; break;
        }
        emit colorRequested(color);
        return;
    }

    QWidget::keyPressEvent(event);
}

void QuickLookWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
}

bool QuickLookWindow::eventFilter(QObject* watched, QEvent* event) {
    bool hasTextEditViewport = m_textEdit && m_textEdit->viewport();

    if ((watched == m_textEdit || (hasTextEditViewport && watched == m_textEdit->viewport()) || watched == m_graphicsView) && event->type() == QEvent::MouseButtonDblClick) {
        // 2026-11-xx：如果在 QuickLookWindow 界面（或其内的视图）双击时，直接关闭窗口
        closePreview();
        return true;
    }

    if ((watched == m_textEdit || (hasTextEditViewport && watched == m_textEdit->viewport()) || watched == m_graphicsView) && event->type() == QEvent::ContextMenu) {
        showContextMenu(static_cast<QContextMenuEvent*>(event)->globalPos());
        return true;
    }

    if ((watched == m_textEdit || (hasTextEditViewport && watched == m_textEdit->viewport()) || watched == m_graphicsView) && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        bool intercept = false;
        int key = keyEvent->key();
        Qt::KeyboardModifiers mods = keyEvent->modifiers();
        
        if (key == Qt::Key_Space || key == Qt::Key_Escape) {
            intercept = true;
        } else if (key == Qt::Key_W && (mods & Qt::ControlModifier)) {
            intercept = true;
        } else if (key == Qt::Key_Up || key == Qt::Key_Left || key == Qt::Key_Down || key == Qt::Key_Right) {
            intercept = true;
        } else if (key >= Qt::Key_1 && key <= Qt::Key_5 && !(mods & Qt::AltModifier)) {
            intercept = true;
        } else if ((mods & Qt::AltModifier) && key >= Qt::Key_1 && key <= Qt::Key_9) {
            intercept = true;
        }
        
        if (intercept) {
            keyPressEvent(keyEvent);
            return true; // 彻底物理截断，防止被子控件内部吞没
        }
    }

    if (event->type() == QEvent::WindowDeactivate) {
        if (m_ignoreDeactivate) {
            return true;
        }
        closePreview();
    }
    return QWidget::eventFilter(watched, event);
}

void QuickLookWindow::contextMenuEvent(QContextMenuEvent* event) {
    showContextMenu(event->globalPos());
}

void QuickLookWindow::showContextMenu(const QPoint& globalPos) {
    if (m_currentPath.isEmpty()) return;

    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);

    // 14 项选项
    QAction* actPrev = menu.addAction("上一个");
    QAction* actNext = menu.addAction("下一个");
    menu.addSeparator();

    QAction* actRotate = menu.addAction("旋转");
    QAction* actFlip = menu.addAction("水平翻转");
    QAction* actOrig = menu.addAction("原始");
    QAction* actFit = menu.addAction("自适应");
    menu.addSeparator();

    QAction* actOpenDefault = menu.addAction("用系统默认程序打开");
    QAction* actShowExplorer = menu.addAction("在”资源管理器”中显示");
    menu.addSeparator();

    QAction* actCopy = menu.addAction("复制");
    QAction* actCut = menu.addAction("剪切");
    QAction* actDel = menu.addAction("删除");
    menu.addSeparator();

    QAction* actCopyName = menu.addAction("复制文件名");
    QAction* actCopyPath = menu.addAction("复制路径");
    QAction* actFavorite = menu.addAction("添加至收藏夹");

    // 根据是否显示图片启用/禁用 旋转、水平翻转、原始、自适应
    bool isImage = m_graphicsView->isVisible();
    actRotate->setEnabled(isImage);
    actFlip->setEnabled(isImage);
    actOrig->setEnabled(isImage);
    actFit->setEnabled(isImage);

    QAction* selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == actPrev) {
        emit prevRequested();
    } else if (selected == actNext) {
        emit nextRequested();
    } else if (selected == actRotate) {
        m_graphicsView->rotateClockwise();
    } else if (selected == actFlip) {
        m_graphicsView->flipHorizontal();
    } else if (selected == actOrig) {
        m_graphicsView->setZoomOriginal();
    } else if (selected == actFit) {
        m_graphicsView->fitImage();
    } else if (selected == actOpenDefault) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentPath));
    } else if (selected == actShowExplorer) {
        ShellHelper::openInExplorer(m_currentPath);
    } else if (selected == actCopy) {
        QList<QUrl> urls;
        urls << QUrl::fromLocalFile(m_currentPath);
        QMimeData* mime = new QMimeData();
        mime->setUrls(urls);
        QApplication::clipboard()->setMimeData(mime);
    } else if (selected == actCut) {
        QList<QUrl> urls;
        urls << QUrl::fromLocalFile(m_currentPath);
        QMimeData* mime = new QMimeData();
        mime->setUrls(urls);
        QByteArray effectData;
        effectData.append((char)2);
        mime->setData("Preferred DropEffect", effectData);
        QApplication::clipboard()->setMimeData(mime);
    } else if (selected == actDel) {
        emit deleteRequested(m_currentPath);
    } else if (selected == actCopyName) {
        QApplication::clipboard()->setText(QFileInfo(m_currentPath).fileName());
    } else if (selected == actCopyPath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(m_currentPath));
    } else if (selected == actFavorite) {
        emit favoriteRequested(m_currentPath);
    }
}

} // namespace QuarkMeta
