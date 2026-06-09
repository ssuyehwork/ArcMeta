#include "ColorPickerWindow.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "../core/DatabaseManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QCursor>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include <QPainter>
#include <QBuffer>
#include <QImageReader>
#include <QSet>
#include <QScrollArea>
#include <QFrame>
#include <QTimer>
#include <QFileDialog>
#include <QMenu>
#include <QProcess>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QPainterPath>
#include <cmath>
#include <algorithm>
#include <QtMath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QColorDialog>
#include <QGridLayout>
#include <QStackedWidget>
#include <QSlider>
#include "FlowLayout.h"
#include "PixelRulerOverlay.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ----------------------------------------------------------------------------
// ToolTipOverlay: 自定义绘制的 Tooltip 覆盖层
// ----------------------------------------------------------------------------
// 已移至 ToolTipOverlay.h

// ----------------------------------------------------------------------------
// ScreenColorPickerOverlay: 屏幕取色器 (多显示器/HighDPI 稳定版)
// ----------------------------------------------------------------------------
class ScreenColorPickerOverlay : public QWidget {
    Q_OBJECT
    struct ScreenCapture {
        QPixmap pixmap;
        QImage image;
        QRect geometry;
        qreal dpr;
    };
public:
    explicit ScreenColorPickerOverlay(std::function<void(QString)> callback, QWidget* parent = nullptr) 
        : QWidget(parent), m_callback(callback) 
    {
        setObjectName("ScreenColorPickerOverlay");
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground); 
        
        QRect totalRect;
        const auto screens = QGuiApplication::screens();
        for (QScreen* screen : screens) {
            QRect geom = screen->geometry();
            totalRect = totalRect.united(geom);
            
            ScreenCapture cap;
            cap.geometry = geom;
            cap.dpr = screen->devicePixelRatio();
            cap.pixmap = screen->grabWindow(0, 0, 0, geom.width(), geom.height());
            cap.pixmap.setDevicePixelRatio(cap.dpr);
            cap.image = cap.pixmap.toImage();
            m_captures.append(cap);
        }
        setGeometry(totalRect);

        setCursor(Qt::BlankCursor);
        setMouseTracking(true);
        
        // 2026-03-xx 优化：不再使用高频定时器，改为按需刷新，极致节省 CPU
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        QTimer::singleShot(50, this, [this]() {
            if (isVisible()) {
                this->grabMouse();
                this->grabKeyboard();
            }
        });
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (m_callback) m_callback(m_currentColorHex);
            
            // 2026-03-xx 按照用户要求，取色时将 HEX 写入剪贴板
            QApplication::clipboard()->setText(m_currentColorHex);

            // 2026-03-13 按照用户要求：提示时长缩短为 700ms
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("已复制色码: %1\n(右键可退出取色模式)").arg(m_currentColorHex), 700);

            // 2026-03-xx 按照用户最新要求：恢复连续吸色功能，左键点击不自动关闭窗口，仅由右键或 Esc 退出
        } else if (event->button() == Qt::RightButton) {
            // [用户修改要求] 拦截右键按下，统一在 Release 中处理取消逻辑，防止事件穿透到第三方应用触发菜单
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::RightButton) {
            // [用户修改要求] 右键单击触发放弃任务逻辑，且必须在 Release 时处理以完全拦截点击流，防止穿透
            cancelPicker();
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        update(); // 仅在鼠标移动时重绘，极大缓解连续操作下的 UI 压力
        QWidget::mouseMoveEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            cancelPicker();
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        
        QPoint globalPos = QCursor::pos();
        QPoint localPos = mapFromGlobal(globalPos);
        
        // 1. 寻找当前鼠标所在的屏幕捕捉
        const ScreenCapture* currentCap = nullptr;
        for (const auto& cap : m_captures) {
            if (cap.geometry.contains(globalPos)) {
                currentCap = &cap;
                break;
            }
        }
        if (!currentCap && !m_captures.isEmpty()) currentCap = &m_captures[0];
        if (!currentCap) return;

        // 2. 绘制所有屏幕背景 (保持相对位置)
        for (const auto& cap : m_captures) {
            p.drawPixmap(cap.geometry.topLeft() - geometry().topLeft(), cap.pixmap);
        }

        // 3. 采样颜色：使用物理像素坐标，使用 qFloor 确保对齐
        QPoint relativePos = globalPos - currentCap->geometry.topLeft();
        // [CRITICAL] 精确采样坐标计算：必须结合 DPR 并使用 qFloor，防止缩放环境下采样点发生亚像素偏移。
        QPoint pixelPos(qFloor(relativePos.x() * currentCap->dpr), qFloor(relativePos.y() * currentCap->dpr));
        
        QColor centerColor = Qt::black;
        if (pixelPos.x() >= 0 && pixelPos.x() < currentCap->image.width() && 
            pixelPos.y() >= 0 && pixelPos.y() < currentCap->image.height()) {
            centerColor = currentCap->image.pixelColor(pixelPos);
        }
        centerColor.setAlpha(255); // 强制不透明，确保预览颜色准确
        m_currentColorHex = centerColor.name().toUpper();

        // 4. 更新光标样式为针筒
        QString syringeColor = (centerColor.lightness() > 128) ? "#000000" : "#FFFFFF";
        if (syringeColor != m_lastSyringeColor) {
            QPixmap syringe = IconHelper::getIcon("screen_picker", syringeColor).pixmap(32, 32);
            setCursor(QCursor(syringe, 3, 29)); // 针尖对准点击位置
            m_lastSyringeColor = syringeColor;
        }

        // 5. 绘制放大镜
        int grabRadius = 8;
        int grabSize = grabRadius * 2 + 1;
        int lensSize = 160; 
        
        int lensX = localPos.x() + 25;
        int lensY = localPos.y() + 25;
        if (lensX + lensSize > width()) lensX = localPos.x() - lensSize - 25;
        if (lensY + lensSize > height()) lensY = localPos.y() - lensSize - 25;

        QRect lensRect(lensX, lensY, lensSize, lensSize);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(lensRect.adjusted(3, 3, 3, 3), 10, 10);

        QPainterPath path;
        path.addRoundedRect(lensRect, 10, 10);
        // 核心修复：移除暗色背景填充，改用黑色底色，并确保像素网格完全覆盖
        p.fillPath(path, Qt::black); 
        p.setPen(QPen(QColor(100, 100, 100), 2));
        p.drawPath(path);

        // 绘制像素网格 (确保采样源一致且不透明)
        p.save();
        QRect gridArea = lensRect.adjusted(2, 2, -2, -50);
        p.setClipRect(gridArea);
        // [CRITICAL] 核心修复：必须关闭抗锯齿，防止高 DPI 下像素块边缘颜色插值导致采样色变暗或模糊。
        p.setRenderHint(QPainter::Antialiasing, false);
        // [CRITICAL] 核心修复：必须清除之前的画刷残留，否则 drawRect 会使用旧的半透明阴影画刷对像素格进行二次填充。
        p.setBrush(Qt::NoBrush);

        for(int j = 0; j < grabSize; ++j) {
            for(int i = 0; i < grabSize; ++i) {
                int px = pixelPos.x() + i - grabRadius;
                int py = pixelPos.y() + j - grabRadius;
                QColor c = Qt::black;
                if (px >= 0 && px < currentCap->image.width() && py >= 0 && py < currentCap->image.height()) {
                    c = currentCap->image.pixelColor(px, py);
                }
                c.setAlpha(255);
                
                int x1 = gridArea.left() + i * gridArea.width() / grabSize;
                int x2 = gridArea.left() + (i + 1) * gridArea.width() / grabSize;
                int y1 = gridArea.top() + j * gridArea.height() / grabSize;
                int y2 = gridArea.top() + (j + 1) * gridArea.height() / grabSize;
                
                p.fillRect(x1, y1, x2 - x1, y2 - y1, c);
                p.setPen(QPen(QColor(255, 255, 255, 15), 1));
                p.drawRect(x1, y1, x2 - x1, y2 - y1);
            }
        }
        
        // 中心高亮框，同样使用精确计算的坐标
        int cx1 = gridArea.left() + grabRadius * gridArea.width() / grabSize;
        int cx2 = gridArea.left() + (grabRadius + 1) * gridArea.width() / grabSize;
        int cy1 = gridArea.top() + grabRadius * gridArea.height() / grabSize;
        int cy2 = gridArea.top() + (grabRadius + 1) * gridArea.height() / grabSize;
        p.setPen(QPen(Qt::red, 2));
        p.drawRect(cx1, cy1, cx2 - cx1, cy2 - cy1);
        p.restore();

        // 信息栏：预览色块必须与 centerColor 完全一致
        QRect infoRect = lensRect;
        infoRect.setTop(lensRect.bottom() - 50);
        p.setPen(QPen(QColor(176, 176, 176), 1));
        p.drawLine(infoRect.left(), infoRect.top(), infoRect.right(), infoRect.top());

        QRect colorRect(infoRect.left() + 10, infoRect.top() + 12, 26, 26);
        p.setBrush(centerColor); // 使用 Brush 确保填充效果
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(colorRect);

        p.setPen(Qt::white);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(14);
        p.setFont(font);
        p.drawText(infoRect.left() + 45, infoRect.top() + 22, m_currentColorHex);

        font.setPixelSize(11);
        font.setBold(false);
        p.setFont(font);
        QString rgbText = QString("RGB: %1, %2, %3").arg(centerColor.red()).arg(centerColor.green()).arg(centerColor.blue());
        p.drawText(infoRect.left() + 45, infoRect.top() + 40, rgbText);
    }

private:
    void cancelPicker() {
        releaseMouse();
        releaseKeyboard();
        // 2026-03-xx 核心修复：显式清理图片资源，立即释放高额内存
        // [MODIFIED] 增加显式置空以确保大对象被标记为可回收
        for (auto& cap : m_captures) {
            cap.pixmap = QPixmap();
            cap.image = QImage();
        }
        m_captures.clear();
        close();
    }

    std::function<void(QString)> m_callback;
    QString m_currentColorHex = "#FFFFFF";
    QString m_lastSyringeColor;
    QList<ScreenCapture> m_captures;
};


// ----------------------------------------------------------------------------
// ColorWheel: 基于 HSV 的圆形色轮
// ----------------------------------------------------------------------------
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(320, 320);
        // [FIX] 使用 ARGB32_Premultiplied 支持透明背景，Format_RGB32 会导致 transparent 变成黑色
        m_wheelImg = QImage(320, 320, QImage::Format_ARGB32_Premultiplied);
        m_wheelImg.fill(Qt::transparent);
        
        int center = 160;
        int radius = 150;
        for (int y = 0; y < 320; ++y) {
            for (int x = 0; x < 320; ++x) {
                int dx = x - center;
                // [FIX] 翻转 y 轴，使得红色(0度)在右侧，与其通常习惯一致（可选，保持原样也可，这里仅优化计算）
                int dy = y - center; 
                double dist = std::sqrt((double)dx*dx + (double)dy*dy);
                
                // [FIX] 增加简单的边缘抗锯齿 (1px 范围内 alpha 渐变)
                if (dist <= radius + 1.0) {
                    double angle = std::atan2((double)dy, (double)dx);
                    double hue = (angle + M_PI) / (2 * M_PI);
                    double sat = dist / radius;
                    if (sat > 1.0) sat = 1.0; // clamp for internal correctness within AA region

                    QColor c = QColor::fromHsvF(hue, sat, 1.0);
                    
                    // 边缘平滑处理
                    if (dist > radius - 1.0) {
                        double alpha = (radius + 1.0 - dist) / 2.0; // Simple linear falloff
                        if (alpha > 1.0) alpha = 1.0;
                        if (alpha < 0.0) alpha = 0.0;
                        c.setAlphaF(alpha);
                    }
                    
                    m_wheelImg.setPixelColor(x, y, c);
                }
            }
        }
    }

signals:
    void colorChanged(double hue, double sat);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing); // [FIX] 开启抗锯齿，让选中点圆圈更平滑
        p.drawImage(0, 0, m_wheelImg);
        if (m_hue >= 0) {
            int center = 160;
            int radius = 150;
            double angle = m_hue * 2 * M_PI - M_PI;
            double dist = m_sat * radius;
            int cx = center + std::cos(angle) * dist;
            int cy = center + std::sin(angle) * dist;
            p.setPen(QPen(Qt::black, 2));
            p.setBrush(Qt::white);
            p.drawEllipse(QPoint(cx, cy), 6, 6);
        }
    }

    void mousePressEvent(QMouseEvent* event) override { handleMouse(event); }
    void mouseMoveEvent(QMouseEvent* event) override { if (event->buttons() & Qt::LeftButton) handleMouse(event); }

private:
    void handleMouse(QMouseEvent* event) {
        int center = 160;
        int dx = event->pos().x() - center;
        int dy = event->pos().y() - center;
        double dist = std::sqrt((double)dx*dx + (double)dy*dy);
        if (dist <= 160) {
            double angle = std::atan2((double)dy, (double)dx);
            m_hue = (angle + M_PI) / (2 * M_PI);
            m_sat = std::min(dist / 150.0, 1.0);
            emit colorChanged(m_hue, m_sat);
            update();
        }
    }
    QImage m_wheelImg;
    double m_hue = 0.0;
    double m_sat = 0.0;
};

// ----------------------------------------------------------------------------
// ColorPickerDialog: 独立选色弹窗
// ----------------------------------------------------------------------------
class ColorPickerDialog : public QDialog {
    Q_OBJECT
public:
    ColorPickerDialog(QWidget* parent, std::function<void(QString)> callback) 
        : QDialog(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_callback(callback) 
    {
        setFixedSize(500, 600);
        setAttribute(Qt::WA_TranslucentBackground);
        
        auto* container = new QWidget(this);
        container->setObjectName("container");
        container->setStyleSheet("QWidget#container { background: #1a1a1a; border-radius: 15px; border: 1px solid #444; }");
        auto* mainL = new QVBoxLayout(this);
        mainL->setContentsMargins(0,0,0,0);
        mainL->addWidget(container);

        auto* l = new QVBoxLayout(container);
        l->setContentsMargins(20, 20, 20, 20);
        l->setSpacing(15);

        auto* headerLayout = new QHBoxLayout();
        // [FIX] 使用占位控件平衡右侧关闭按钮，确保标题视觉绝对居中
        auto* spacerBtn = new QWidget();
        spacerBtn->setFixedSize(28, 28);
        spacerBtn->setStyleSheet("background: transparent;");
        headerLayout->addWidget(spacerBtn);

        headerLayout->addStretch();
        auto* title = new QLabel("颜色选择器");
        title->setStyleSheet("color: white; font-weight: bold; font-size: 16px; border: none; background: transparent;");
        headerLayout->addWidget(title);
        headerLayout->addStretch();

        auto* closeTopBtn = new QPushButton();
        closeTopBtn->setFixedSize(28, 28);
        closeTopBtn->setIcon(IconHelper::getIcon("close", "#aaaaaa", 16));
        closeTopBtn->setCursor(Qt::PointingHandCursor);
        closeTopBtn->setStyleSheet("QPushButton { background: transparent; border-radius: 4px; border: none; } QPushButton:hover { background-color: #E81123; }");
        connect(closeTopBtn, &QPushButton::clicked, this, &QDialog::reject);
        headerLayout->addWidget(closeTopBtn);

        l->addLayout(headerLayout);

        m_wheel = new ColorWheel();
        connect(m_wheel, &ColorWheel::colorChanged, this, &ColorPickerDialog::onWheelChanged);
        l->addWidget(m_wheel, 0, Qt::AlignCenter);

        auto* bRow = new QHBoxLayout();
        auto* blbl = new QLabel("亮度:");
        blbl->setStyleSheet("color: white; border: none; background: transparent;");
        bRow->addWidget(blbl);
        m_brightSlider = new QSlider(Qt::Horizontal);
        m_brightSlider->setRange(0, 100);
        m_brightSlider->setValue(100);
        m_brightSlider->setStyleSheet("QSlider::groove:horizontal { height: 6px; background: #444; border-radius: 3px; } QSlider::handle:horizontal { background: white; width: 16px; margin: -5px 0; border-radius: 8px; }");
        connect(m_brightSlider, &QSlider::valueChanged, this, &ColorPickerDialog::updatePreview);
        bRow->addWidget(m_brightSlider);
        l->addLayout(bRow);

        m_preview = new QFrame();
        m_preview->setFixedHeight(60);
        m_preview->setStyleSheet("border-radius: 10px; background: white; border: 1px solid #555;");
        auto* pl = new QVBoxLayout(m_preview);
        m_hexLabel = new QLabel("#FFFFFF");
        m_hexLabel->setStyleSheet("color: #1a1a1a; font-weight: bold; font-size: 18px; border: none; background: transparent;");
        m_hexLabel->setAlignment(Qt::AlignCenter);
        pl->addWidget(m_hexLabel);
        l->addWidget(m_preview);

        auto* btnRow = new QHBoxLayout();
        auto* btnClose = new QPushButton("取消");
        btnClose->setAutoDefault(false);
        btnClose->setFixedHeight(36);
        btnClose->setStyleSheet("background: #444; color: white; border-radius: 6px; border: none;");
        connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
        btnRow->addWidget(btnClose);
        
        auto* btnConfirm = new QPushButton("确认选择");
        btnConfirm->setAutoDefault(false);
        btnConfirm->setFixedHeight(36);
        btnConfirm->setStyleSheet("background: #3b8ed0; color: white; border-radius: 6px; border: none; font-weight: bold;");
        connect(btnConfirm, &QPushButton::clicked, this, &ColorPickerDialog::onConfirm);
        btnRow->addWidget(btnConfirm);
        l->addLayout(btnRow);
    }

private slots:
    void onWheelChanged(double h, double s) { m_hue = h; m_sat = s; updatePreview(); }
    void updatePreview() {
        QColor c = QColor::fromHsvF(m_hue, m_sat, m_brightSlider->value() / 100.0);
        m_selectedHex = c.name().toUpper();
        m_preview->setStyleSheet(QString("border-radius: 10px; background: %1; border: 1px solid #555;").arg(m_selectedHex));
        m_hexLabel->setText(m_selectedHex);
        m_hexLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 18px; border: none; background: transparent;")
            .arg(c.lightness() > 128 ? "#1a1a1a" : "white"));
    }
    void onConfirm() { m_callback(m_selectedHex); accept(); }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }

private:
    std::function<void(QString)> m_callback;
    ColorWheel* m_wheel;
    QSlider* m_brightSlider;
    QFrame* m_preview;
    QLabel* m_hexLabel;
    double m_hue = 0, m_sat = 0;
    QString m_selectedHex = "#FFFFFF";
};

// ----------------------------------------------------------------------------
// ColorPickerWindow 实现
// ----------------------------------------------------------------------------

ColorPickerWindow::ColorPickerWindow(QWidget* parent)
    : FramelessDialog("颜色提取器", parent)
{
    setObjectName("ColorPickerWindow");
    setWindowTitle("颜色提取器");
    // [CRITICAL] 缩小窗口默认大小以适应更多屏幕。从 1400x900 调整。
    resize(1000, 750);
    setMinimumSize(850, 600);
    setAcceptDrops(true);
    m_favorites = loadFavorites();
    loadWindowSettings();
    initUI();
    QSettings s("RapidNotes", "ColorPicker");
    QString lastColor = s.value("lastColor", "#D64260").toString();
    useColor(lastColor);
}

ColorPickerWindow::~ColorPickerWindow() {
    saveFavorites();
}

void ColorPickerWindow::initUI() {
    setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei", "Segoe UI", sans-serif; color: #E0E0E0; }
        QLineEdit { background-color: #2A2A2A; border: 1px solid #444; color: #FFFFFF; border-radius: 6px; padding: 4px; font-weight: bold; }
        QLineEdit:focus { border: 1px solid #3b8ed0; }
        QPushButton { background-color: #333; border: 1px solid #444; border-radius: 4px; padding: 6px; outline: none; }
        QPushButton:hover { background-color: #3e3e42; } // 2026-03-xx 统一悬停色
        QPushButton:pressed { background-color: #222; }
        QScrollArea { background: transparent; border: none; }
        QScrollBar:vertical { background: transparent; width: 8px; }
        QScrollBar::handle:vertical { background: #444; border-radius: 4px; min-height: 20px; }
        QScrollBar::handle:vertical:hover { background: #555; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
    )");

    auto* mainVLayout = new QVBoxLayout(m_contentArea);
    mainVLayout->setContentsMargins(20, 20, 20, 20);
    mainVLayout->setSpacing(15);

    // --- 第一排：颜色预览 ①、输入框 ②、输入框 ③、工具按钮 ④ ---
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(10);

    // ① 颜色预览 (200x50)
    auto* previewContainer = new QFrame();
    previewContainer->setFixedSize(200, 50);
    previewContainer->setStyleSheet("background: #1e1e1e; border-radius: 8px; border: 1px solid #333;");
    auto* pl = new QVBoxLayout(previewContainer);
    pl->setContentsMargins(2, 2, 2, 2);
    m_colorDisplay = new QWidget();
    m_colorDisplay->setObjectName("mainPreview");
    m_colorDisplay->setStyleSheet("border-radius: 6px; background: #D64260;");
    auto* cl = new QVBoxLayout(m_colorDisplay);
    cl->setContentsMargins(0, 0, 0, 0);
    m_colorLabel = new QLabel("#D64260");
    m_colorLabel->setCursor(Qt::PointingHandCursor);
    m_colorLabel->setAlignment(Qt::AlignCenter);
    m_colorLabel->setStyleSheet("font-family: Consolas; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    // m_colorLabel->setToolTip("当前 HEX 颜色");
    m_colorLabel->setProperty("tooltipText", "当前 HEX 颜色");
    m_colorLabel->installEventFilter(this);
    cl->addWidget(m_colorLabel);
    pl->addWidget(m_colorDisplay);
    row1->addWidget(previewContainer);

    // ② HEX 框 (含复制按钮)
    auto* hexLayout = new QHBoxLayout();
    hexLayout->setSpacing(2);
    m_hexEntry = new QLineEdit();
    m_hexEntry->setPlaceholderText("HEX");
    m_hexEntry->setFixedWidth(80);
    m_hexEntry->setFixedHeight(36);
    m_hexEntry->setAlignment(Qt::AlignCenter);
    // m_hexEntry->setToolTip("输入 HEX 代码并回车应用");
    m_hexEntry->setProperty("tooltipText", "输入 HEX 代码并回车应用");
    m_hexEntry->installEventFilter(this);
    connect(m_hexEntry, &QLineEdit::returnPressed, this, &ColorPickerWindow::applyHexColor);
    hexLayout->addWidget(m_hexEntry);
    
    auto* btnCopyHex = new QPushButton();
    btnCopyHex->setAutoDefault(false);
    btnCopyHex->setIcon(IconHelper::getIcon("copy", "#CCCCCC"));
    btnCopyHex->setFixedSize(28, 36);
    // btnCopyHex->setToolTip("复制 HEX 代码");
    btnCopyHex->setProperty("tooltipText", "复制 HEX 代码");
    btnCopyHex->installEventFilter(this);
    btnCopyHex->setStyleSheet("QPushButton { background: transparent; border: none; } QPushButton:hover { background: rgba(255,255,255,0.1); }");
    connect(btnCopyHex, &QPushButton::clicked, this, &ColorPickerWindow::copyHexValue);
    hexLayout->addWidget(btnCopyHex);
    row1->addLayout(hexLayout);

    // ③ RGB 框 (含复制按钮)
    auto* rgbContainer = new QWidget();
    rgbContainer->setFixedHeight(36);
    auto* rl = new QHBoxLayout(rgbContainer);
    rl->setContentsMargins(0, 0, 0, 0); rl->setSpacing(2);
    m_rEntry = new QLineEdit(); m_rEntry->setFixedWidth(35); m_rEntry->setFixedHeight(36); m_rEntry->setAlignment(Qt::AlignCenter); m_rEntry->setPlaceholderText("R");
    m_gEntry = new QLineEdit(); m_gEntry->setFixedWidth(35); m_gEntry->setFixedHeight(36); m_gEntry->setAlignment(Qt::AlignCenter); m_gEntry->setPlaceholderText("G");
    m_bEntry = new QLineEdit(); m_bEntry->setFixedWidth(35); m_bEntry->setFixedHeight(36); m_bEntry->setAlignment(Qt::AlignCenter); m_bEntry->setPlaceholderText("B");
    m_rEntry->installEventFilter(this);
    m_gEntry->installEventFilter(this);
    m_bEntry->installEventFilter(this);
    connect(m_rEntry, &QLineEdit::returnPressed, this, &ColorPickerWindow::applyRgbColor);
    connect(m_gEntry, &QLineEdit::returnPressed, this, &ColorPickerWindow::applyRgbColor);
    connect(m_bEntry, &QLineEdit::returnPressed, this, &ColorPickerWindow::applyRgbColor);
    rl->addWidget(m_rEntry);
    rl->addWidget(m_gEntry);
    rl->addWidget(m_bEntry);
    
    auto* btnCopyRgb = new QPushButton();
    btnCopyRgb->setAutoDefault(false);
    btnCopyRgb->setIcon(IconHelper::getIcon("copy", "#CCCCCC"));
    btnCopyRgb->setFixedSize(28, 36);
    // btnCopyRgb->setToolTip("复制 RGB 代码");
    btnCopyRgb->setProperty("tooltipText", "复制 RGB 代码");
    btnCopyRgb->installEventFilter(this);
    btnCopyRgb->setStyleSheet("QPushButton { background: transparent; border: none; } QPushButton:hover { background: rgba(255,255,255,0.1); }");
    connect(btnCopyRgb, &QPushButton::clicked, this, &ColorPickerWindow::copyRgbValue);
    rl->addWidget(btnCopyRgb);
    row1->addWidget(rgbContainer);

    // ④ 工具按钮 (整合进首排)
    auto* toolsLayout = new QHBoxLayout();
    toolsLayout->setSpacing(6);
    auto createToolBtn = [&](const QString& iconName, std::function<void()> cmd, QString color, QString tip) {
        auto* btn = new QPushButton();
        btn->setAutoDefault(false);
        btn->setIcon(IconHelper::getIcon(iconName, "#FFFFFF"));
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(36, 36);
        btn->setStyleSheet(QString("QPushButton { background: %1; border: none; border-radius: 6px; } QPushButton:hover { opacity: 0.8; }").arg(color));
        // btn->setToolTip(tip);
        btn->setProperty("tooltipText", tip);
        btn->installEventFilter(this);
        connect(btn, &QPushButton::clicked, cmd);
        toolsLayout->addWidget(btn);
    };
    createToolBtn("palette", [this](){ openColorPicker(); }, "#3b8ed0", "色轮");
    createToolBtn("screen_picker", [this](){ startScreenPicker(); }, "#9b59b6", "吸色器");
    createToolBtn("pixel_ruler", [this](){ openPixelRuler(); }, "#e67e22", "标尺");
    createToolBtn("image", [this](){ extractFromImage(); }, "#2ecc71", "提取图片");
    createToolBtn("Favorite", [this](){ addToFavorites(); }, "#f39c12", "收藏");
    row1->addLayout(toolsLayout);
    
    row1->addStretch();
    mainVLayout->addLayout(row1);

    // --- 第二排：渐变生成器 ⑤ (摊平成一行) ---
    auto* gradBox = new QFrame();
    gradBox->setObjectName("gradBox");
    gradBox->setFixedHeight(50);
    gradBox->setStyleSheet("QFrame#gradBox { background: #252526; border-radius: 8px; border: 1px solid #383838; }");
    auto* gl = new QHBoxLayout(gradBox);
    gl->setContentsMargins(15, 0, 15, 0);
    gl->setSpacing(8);

    auto* gt = new QLabel("渐变生成器");
    gt->setStyleSheet("font-weight: bold; font-size: 12px; color: #888; background: transparent;");
    gl->addWidget(gt);

    auto addGradInput = [&](const QString& label, QLineEdit*& entry, int width) {
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet("font-size: 11px; color: #666; background: transparent;");
        gl->addWidget(lbl);
        entry = new QLineEdit();
        entry->setFixedWidth(width);
        entry->setFixedHeight(28);
        entry->installEventFilter(this);
        gl->addWidget(entry);
    };
    addGradInput("起始", m_gradStart, 80);
    addGradInput("结束", m_gradEnd, 80);
    
    auto* stepslbl = new QLabel("步数");
    stepslbl->setStyleSheet("color: #666; font-size: 11px; background: transparent; border: none;");
    gl->addWidget(stepslbl);
    
    m_gradSteps = new QLineEdit("7");
    m_gradSteps->setFixedWidth(77);
    m_gradSteps->setFixedHeight(28);
    m_gradSteps->setAlignment(Qt::AlignCenter);
    m_gradSteps->installEventFilter(this);
    gl->addWidget(m_gradSteps);
    
    auto* sl = new QVBoxLayout();
    sl->setContentsMargins(0, 4, 0, 4);
    sl->setSpacing(0);
    
    QString spinBtnStyle = "QPushButton { background: transparent; border: none; padding: 0px; margin: 0px; } "
                           "QPushButton:hover { background: rgba(255, 255, 255, 0.1); border-radius: 2px; }";

    auto* btnUp = new QPushButton();
    btnUp->setFixedSize(16, 12);
    btnUp->setCursor(Qt::PointingHandCursor);
    btnUp->setIcon(IconHelper::getIcon("arrow_up", "#aaaaaa", 12));
    btnUp->setIconSize(QSize(12, 12));
    btnUp->setStyleSheet(spinBtnStyle);
    connect(btnUp, &QPushButton::clicked, [this](){
        int val = m_gradSteps->text().toInt();
        m_gradSteps->setText(QString::number(val + 1));
    });
    
    auto* btnDown = new QPushButton();
    btnDown->setFixedSize(16, 12);
    btnDown->setCursor(Qt::PointingHandCursor);
    btnDown->setIcon(IconHelper::getIcon("arrow_down", "#aaaaaa", 12));
    btnDown->setIconSize(QSize(12, 12));
    btnDown->setStyleSheet(spinBtnStyle);
    connect(btnDown, &QPushButton::clicked, [this](){
        int val = m_gradSteps->text().toInt();
        if (val > 2) m_gradSteps->setText(QString::number(val - 1));
    });
    
    sl->addWidget(btnUp);
    sl->addWidget(btnDown);
    
    gl->addSpacing(-6); 
    gl->addLayout(sl);

    gl->addSpacing(5);
    auto createModeBtn = [&](const QString& mode) {
        auto* btn = new QPushButton(mode);
        btn->setAutoDefault(false);
        btn->setCheckable(true);
        btn->setFixedWidth(45);
        btn->setFixedHeight(26);
        btn->setStyleSheet(
            "QPushButton { background: #333; border: 1px solid #444; border-radius: 4px; font-size: 11px; padding: 0; } "
            "QPushButton:hover { background: #444; } "
            "QPushButton:checked { background: #007ACC; color: white; border-color: #007ACC; }"
        );
        if (m_gradMode == mode) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, [this, mode, gradBox](){
            m_gradMode = mode;
            for(auto* b : gradBox->findChildren<QPushButton*>()) {
                if(b->isCheckable()) b->setChecked(b->text() == mode);
            }
        });
        return btn;
    };
    gl->addWidget(createModeBtn("变暗"));
    gl->addWidget(createModeBtn("变亮"));
    gl->addWidget(createModeBtn("饱和"));

    auto* btnGrad = new QPushButton("生成渐变");
    btnGrad->setAutoDefault(false);
    btnGrad->setFixedSize(80, 28);
    btnGrad->setStyleSheet("background: #007ACC; font-weight: bold; color: white; border: none; border-radius: 4px;");
    connect(btnGrad, &QPushButton::clicked, this, &ColorPickerWindow::generateGradient);
    gl->addWidget(btnGrad);
    gl->addStretch();
    
    mainVLayout->addWidget(gradBox);

    // --- 图片预览区域 (从左侧面板移出，由逻辑触发显示) ---
    m_imagePreviewFrame = new QFrame();
    m_imagePreviewFrame->setObjectName("imagePreviewFrame");
    m_imagePreviewFrame->setStyleSheet("QFrame#imagePreviewFrame { background: #1e1e1e; border: 1px dashed #555; border-radius: 12px; }");
    m_imagePreviewFrame->setFixedHeight(120);
    auto* ipl = new QHBoxLayout(m_imagePreviewFrame);
    ipl->setContentsMargins(10, 5, 10, 5);
    m_imagePreviewLabel = new QLabel("暂无图片");
    m_imagePreviewLabel->setAlignment(Qt::AlignCenter);
    m_imagePreviewLabel->setStyleSheet("color: #666; border: none; background: transparent;");
    ipl->addWidget(m_imagePreviewLabel, 1);
    auto* btnClearImg = new QPushButton("重置图片提取");
    btnClearImg->setAutoDefault(false);
    btnClearImg->setFixedSize(120, 30);
    btnClearImg->setStyleSheet("color: #888; border: 1px solid #444; background: #2A2A2A; font-size: 11px; border-radius: 4px;");
    connect(btnClearImg, &QPushButton::clicked, [this](){
        m_imagePreviewFrame->hide();
        m_imagePreviewLabel->setPixmap(QPixmap());
        qDeleteAll(m_extractGridContainer->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
        m_dropHintContainer->show();
        showNotification("已重置图片提取");
    });
    ipl->addWidget(btnClearImg);
    mainVLayout->addWidget(m_imagePreviewFrame);
    m_imagePreviewFrame->hide();

    // --- 第三排：导航切换 ---
    auto* navBar = new QHBoxLayout();
    navBar->setSpacing(10);
    auto createNavBtn = [&](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setAutoDefault(false);
        btn->setFixedHeight(36);
        btn->setFixedWidth(120);
        btn->setStyleSheet(
            "QPushButton { background: #333; border-radius: 6px; font-weight: bold; border: 1px solid #444; } "
            "QPushButton:hover { background: #444; } "
            "QPushButton:checked { background: #007ACC; color: white; border-color: #007ACC; }"
        );
        btn->setCheckable(true);
        connect(btn, &QPushButton::clicked, [this, text, navBar, btn](){ 
            for(int i=0; i<navBar->count(); i++) {
                auto* b = qobject_cast<QPushButton*>(navBar->itemAt(i)->widget());
                if(b) b->setChecked(false);
            }
            btn->setChecked(true);
            switchView(text); 
        });
        if(text=="我的收藏") btn->setChecked(true);
        return btn;
    };
    navBar->addStretch();
    navBar->addWidget(createNavBtn("我的收藏"));
    navBar->addWidget(createNavBtn("渐变预览"));
    navBar->addWidget(createNavBtn("图片提取"));
    navBar->addStretch();
    mainVLayout->addLayout(navBar);

    // --- 第四排：内容区域 (Stacked Widget) ---
    createRightPanel(m_contentArea);
    mainVLayout->addWidget(m_stack, 1);
}

void ColorPickerWindow::createRightPanel(QWidget* parent) {
    m_stack = new QStackedWidget();
    auto createScroll = [&](QWidget*& content) {
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content = new QWidget();
        content->setStyleSheet("background: transparent;");
        scroll->setWidget(content);
        return scroll;
    };
    m_favScroll = createScroll(m_favContent);
    m_gradScroll = createScroll(m_gradContent);
    m_extractScroll = createScroll(m_extractContent);
    
    auto* fl = new QVBoxLayout(m_favContent);
    fl->setContentsMargins(20, 20, 25, 20);
    fl->setSpacing(15);
    auto* ft = new QLabel("我的收藏");
    ft->setStyleSheet("font-size: 20px; font-weight: bold; color: white; border: none;");
    fl->addWidget(ft);
    
    m_favGridContainer = new QFrame();
    m_favGridContainer->setObjectName("cardContainer");
    m_favGridContainer->setStyleSheet("QFrame#cardContainer { background: #252526; border-radius: 12px; }");
    m_favGridContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    new FlowLayout(m_favGridContainer, 10, 5, 5); 
    fl->addWidget(m_favGridContainer);
    fl->addStretch();

    auto* gl = new QVBoxLayout(m_gradContent);
    gl->setContentsMargins(20, 20, 25, 20);
    gl->setSpacing(15);
    auto* gt = new QLabel("渐变预览");
    gt->setStyleSheet("font-size: 20px; font-weight: bold; color: white; border: none;");
    gl->addWidget(gt);

    auto* gt2 = new QLabel("生成结果 (左键应用 / 右键收藏)");
    gt2->setStyleSheet("font-weight: bold; font-size: 14px; border: none; background: transparent; color: #888;");
    gl->addWidget(gt2);

    m_gradGridContainer = new QFrame();
    m_gradGridContainer->setObjectName("cardContainer");
    m_gradGridContainer->setStyleSheet("QFrame#cardContainer { background: #252526; border-radius: 12px; }");
    m_gradGridContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    new FlowLayout(m_gradGridContainer, 10, 5, 5);
    gl->addWidget(m_gradGridContainer);
    gl->addStretch();

    auto* el = new QVBoxLayout(m_extractContent);
    el->setContentsMargins(20, 20, 25, 20);
    el->setSpacing(15);
    auto* et = new QLabel("图片提取");
    et->setStyleSheet("font-size: 20px; font-weight: bold; color: white; border: none;");
    el->addWidget(et);

    auto* et2 = new QLabel("提取结果 (左键应用 / 右键收藏)");
    et2->setStyleSheet("font-weight: bold; font-size: 14px; border: none; background: transparent; color: #888;");
    el->addWidget(et2);

    m_dropHintContainer = new QFrame();
    m_dropHintContainer->setStyleSheet("background: transparent; border: 3px dashed #555; border-radius: 15px;");
    m_dropHintContainer->setFixedHeight(300);
    auto* hl = new QVBoxLayout(m_dropHintContainer);
    hl->setAlignment(Qt::AlignCenter);
    
    auto* iconHint = new QLabel();
    iconHint->setPixmap(IconHelper::getIcon("image", "#444444").pixmap(64, 64));
    iconHint->setAlignment(Qt::AlignCenter);
    iconHint->setStyleSheet("border: none; background: transparent;");
    hl->addWidget(iconHint);

    auto* hint = new QLabel("拖放图片到软件任意位置\n\n或\n\nCtrl+V 粘贴\n点击左侧相机图标");
    hint->setStyleSheet("color: #666; font-size: 16px; border: none; background: transparent; margin-top: 10px;");
    hint->setAlignment(Qt::AlignCenter);
    hl->addWidget(hint);
    el->addWidget(m_dropHintContainer);

    m_extractGridContainer = new QFrame();
    m_extractGridContainer->setObjectName("cardContainer");
    m_extractGridContainer->setStyleSheet("QFrame#cardContainer { background: #252526; border-radius: 12px; }");
    m_extractGridContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    new FlowLayout(m_extractGridContainer, 10, 5, 5);
    el->addWidget(m_extractGridContainer);
    el->addStretch();

    m_stack->addWidget(m_favScroll);
    m_stack->addWidget(m_gradScroll);
    m_stack->addWidget(m_extractScroll);
    
    updateFavoritesDisplay();
}

void ColorPickerWindow::switchView(const QString& value) {
    if (value == "我的收藏") { m_stack->setCurrentWidget(m_favScroll); updateFavoritesDisplay(); }
    else if (value == "渐变预览") { m_stack->setCurrentWidget(m_gradScroll); }
    else if (value == "图片提取") { m_stack->setCurrentWidget(m_extractScroll); }
}

void ColorPickerWindow::updateColorDisplay() {
    m_colorDisplay->setStyleSheet(QString("border-radius: 6px; background: %1;").arg(m_currentColor));
    m_colorLabel->setText(m_currentColor);
    QColor c = hexToColor(m_currentColor);
    m_colorLabel->setStyleSheet(QString("font-family: Consolas; font-size: 18px; font-weight: bold; border: none; background: transparent; color: %1;")
        .arg(c.lightness() > 128 ? "#1a1a1a" : "white"));
    m_hexEntry->setText(m_currentColor);
    m_rEntry->setText(QString::number(c.red()));
    m_gEntry->setText(QString::number(c.green()));
    m_bEntry->setText(QString::number(c.blue()));
    if(m_gradStart->text().isEmpty()) m_gradStart->setText(m_currentColor);
}

void ColorPickerWindow::useColor(const QString& hex) {
    m_currentColor = hex.toUpper();
    QSettings s("RapidNotes", "ColorPicker");
    s.setValue("lastColor", m_currentColor);
    updateColorDisplay();
}

void ColorPickerWindow::applyHexColor() {
    QString h = m_hexEntry->text().trimmed();
    if (!h.startsWith("#")) h = "#" + h;
    QColor c(h);
    if (c.isValid()) useColor(c.name().toUpper());
    else showNotification("无效的 HEX 颜色代码", true);
}

void ColorPickerWindow::applyRgbColor() {
    int r = m_rEntry->text().toInt();
    int g = m_gEntry->text().toInt();
    int b = m_bEntry->text().toInt();
    QColor c(r, g, b);
    if (c.isValid()) useColor(c.name().toUpper());
    else showNotification("RGB 值必须在 0-255 之间", true);
}

void ColorPickerWindow::copyHexValue() {
    ClipboardMonitor::instance().forceNext();
    QApplication::clipboard()->setText(m_currentColor);
    showNotification("已复制 " + m_currentColor);
}

void ColorPickerWindow::copyRgbValue() {
    QColor c = hexToColor(m_currentColor);
    QString rgb = QString("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    ClipboardMonitor::instance().forceNext();
    QApplication::clipboard()->setText(rgb);
    showNotification("已复制 " + rgb);
}

void ColorPickerWindow::startScreenPicker() {
    // 2026-03-xx 核心修复：重复创建保护
    // [MODIFIED] 使用 qApp->topLevelWidgets() 检查全局单例，防止跨父窗口重复创建
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top->objectName() == "ScreenColorPickerOverlay") {
            top->raise();
            top->activateWindow();
            return;
        }
    }

    auto* picker = new ScreenColorPickerOverlay([this](QString hex){
        useColor(hex);
        addSpecificColorToFavorites(hex);
    }, nullptr); // 改为 nullptr 确保作为独立顶层窗口
    picker->show();
}

void ColorPickerWindow::openPixelRuler() {
    // 2026-03-xx 核心修复：单例保护，防止标尺工具多开导致内存溢出
    if (findChild<QWidget*>("PixelRulerOverlay")) {
        return;
    }
    auto* ruler = new PixelRulerOverlay(this);
    ruler->setAttribute(Qt::WA_DeleteOnClose);
    ruler->show();
}

void ColorPickerWindow::openColorPicker() {
    auto* dlg = new ColorPickerDialog(this, [this](QString hex){ useColor(hex); });
    dlg->show();
}

void ColorPickerWindow::addToFavorites() {
    addSpecificColorToFavorites(m_currentColor);
}

void ColorPickerWindow::addSpecificColorToFavorites(const QString& color) {
    if (!m_favorites.contains(color)) {
        m_favorites.prepend(color);
        saveFavorites();
        
        // [OPTIMIZATION] 增量刷新 UI 核心逻辑：
        // 彻底杜绝全量重建，改为在 FlowLayout 头部直接插入新控件
        if (m_stack->currentWidget() == m_favScroll) {
            auto* flow = qobject_cast<FlowLayout*>(m_favGridContainer->layout());
            if (flow) {
                // 如果当前显示的是“暂无收藏”，先清空它
                if (m_favorites.size() == 1) {
                    QLayoutItem *child;
                    while ((child = flow->takeAt(0)) != nullptr) {
                        if (child->widget()) child->widget()->deleteLater();
                        delete child;
                    }
                }
                QWidget* tile = createFavoriteTile(m_favGridContainer, color);
                flow->insertWidget(0, tile);
                m_favGridContainer->updateGeometry();
            }
        }

        showNotification("已收藏: " + color);

        // 同步存入数据库，以便全局查找
        QStringList tags;
        tags << "颜色" << "收藏" << "色码" << "色值" << "颜值" << "颜色码";
        if (color.startsWith("#")) tags << "HEX";
        
        DatabaseManager::instance().addNoteAsync(
            color, color, tags, color, -1, "color"
        );
    } else {
        showNotification(color + " 已在收藏中", true);
    }
}

void ColorPickerWindow::removeFavorite(const QString& color) {
    m_favorites.removeAll(color);
    saveFavorites();
    updateFavoritesDisplay();
}

void ColorPickerWindow::updateFavoritesDisplay() {
    auto* flow = qobject_cast<FlowLayout*>(m_favGridContainer->layout());
    if (!flow) return;
    
    QLayoutItem *child;
    while ((child = flow->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    
    if (m_favorites.isEmpty()) {
        auto* lbl = new QLabel("暂无收藏\n右键点击任何颜色块即可收藏");
        lbl->setStyleSheet("color: #666; font-size: 16px; border: none; background: transparent; padding: 40px;");
        lbl->setAlignment(Qt::AlignCenter);
        flow->addWidget(lbl);
        return;
    }

    for (int i = 0; i < m_favorites.size(); ++i) {
        QWidget* tile = createFavoriteTile(m_favGridContainer, m_favorites[i]);
        flow->addWidget(tile);
    }
    m_favGridContainer->updateGeometry();
}

QWidget* ColorPickerWindow::createFavoriteTile(QWidget* parent, const QString& colorHex) {
    auto* tile = new QFrame(parent);
    // [CRITICAL] 将收藏项改为 30*30 的纯色方块，视觉更整洁
    tile->setFixedSize(30, 30);
    
    tile->setStyleSheet(QString(
        "QFrame { background-color: %1; border-radius: 4px; border: 1px solid rgba(255,255,255,0.1); }"
        "QFrame:hover { border: 1px solid white; }"
    ).arg(colorHex));
    
    // 悬停显示 HEX 值
    // tile->setToolTip(colorHex);
    tile->setProperty("tooltipText", colorHex);
    tile->setCursor(Qt::PointingHandCursor);
    tile->setProperty("color", colorHex);
    tile->installEventFilter(this);

    return tile;
}

void ColorPickerWindow::generateGradient() {
    QString startHex = m_gradStart->text().trimmed();
    if (!startHex.startsWith("#")) startHex = "#" + startHex;
    QColor start = QColor(startHex);
    if (!start.isValid()) { showNotification("起始色无效", true); return; }
    QString endHex = m_gradEnd->text().trimmed();
    int steps = m_gradSteps->text().toInt();
    if (steps < 2) steps = 2;
    QStringList colors;
    if (endHex.isEmpty()) {
        float h, s, v;
        start.getHsvF(&h, &s, &v);
        for (int i = 0; i < steps; ++i) {
            double ratio = (double)i / (steps - 1);
            QColor c;
            if (m_gradMode == "变暗") c = QColor::fromHsvF(h, s, v * (1 - ratio * 0.7));
            else if (m_gradMode == "变亮") c = QColor::fromHsvF(h, s, v + (1 - v) * ratio);
            else if (m_gradMode == "饱和") c = QColor::fromHsvF(h, std::min(1.0f, std::max(0.0f, s + (1 - s) * (float)ratio * (s < 0.5f ? 1.0f : -1.0f))), v);
            colors << c.name().toUpper();
        }
    } else {
        if (!endHex.startsWith("#")) endHex = "#" + endHex;
        QColor end = QColor(endHex);
        if (!end.isValid()) { showNotification("结束色无效", true); return; }
        for (int i = 0; i < steps; ++i) {
            double r = (double)i / (steps - 1);
            int red = start.red() + (end.red() - start.red()) * r;
            int green = start.green() + (end.green() - start.green()) * r;
            int blue = start.blue() + (end.blue() - start.blue()) * r;
            colors << QColor(red, green, blue).name().toUpper();
        }
    }
    
    auto* flow = qobject_cast<FlowLayout*>(m_gradGridContainer->layout());
    if (!flow) return;

    QLayoutItem *child;
    while ((child = flow->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    
    for (int i = 0; i < colors.size(); ++i) {
        QWidget* tile = createColorTile(m_gradGridContainer, colors[i]);
        flow->addWidget(tile);
    }
    m_gradGridContainer->updateGeometry();
    switchView("渐变预览");
}

QWidget* ColorPickerWindow::createColorTile(QWidget* parent, const QString& colorHex) {
    auto* tile = new QFrame(parent);
    // 同步修改为 30*30 色块以保持视觉统一
    tile->setFixedSize(30, 30); 
    
    tile->setStyleSheet(QString(
        "QFrame { background-color: %1; border-radius: 4px; border: 1px solid rgba(255,255,255,0.1); }"
        "QFrame:hover { border: 1px solid white; }"
    ).arg(colorHex));
    
    // tile->setToolTip(colorHex);
    tile->setProperty("tooltipText", colorHex);
    tile->setCursor(Qt::PointingHandCursor);
    tile->setProperty("color", colorHex);
    tile->installEventFilter(this);
    
    return tile;
}

void ColorPickerWindow::extractFromImage() {
    QString path = QFileDialog::getOpenFileName(this, "选择图片", "", "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (!path.isEmpty()) processImage(path);
}

void ColorPickerWindow::processImage(const QString& filePath, const QImage& image) {
    m_currentImagePath = filePath;
    QImage img = image;
    if (img.isNull() && !filePath.isEmpty()) {
        // [CRITICAL] 核心修复：使用 QImageReader 进行流式缩放加载，防止超大图片导致 OOM 崩溃
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        if (reader.canRead()) {
            QSize originalSize = reader.size();
            // 如果图片像素过大（超过 4K 级别），则在加载时直接缩放到 2048 像素以内
            if (originalSize.width() > 2560 || originalSize.height() > 2560) {
                QSize scaledSize = originalSize;
                scaledSize.scale(2048, 2048, Qt::KeepAspectRatio);
                reader.setScaledSize(scaledSize);
            }
            img = reader.read();
        }
    }
    
    if (img.isNull()) {
        if (!filePath.isEmpty()) showNotification("图片加载失败或格式不支持", true);
        return;
    }

    m_imagePreviewFrame->show();
    // [OPTIMIZATION] 先在内存中缩放 QImage 再转 QPixmap，避免大图直接转 Pixmap 导致的瞬时显存/内存爆表
    m_imagePreviewLabel->setPixmap(QPixmap::fromImage(img.scaled(340, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_imagePreviewLabel->setText("");
    
    m_dropHintContainer->hide();
    m_extractGridContainer->show();

    auto* flow = qobject_cast<FlowLayout*>(m_extractGridContainer->layout());
    if (!flow) return;

    QLayoutItem *child;
    while ((child = flow->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    
    QStringList colors = extractDominantColors(img, 24);
    for (int i = 0; i < colors.size(); ++i) {
        QWidget* tile = createColorTile(m_extractGridContainer, colors[i]);
        flow->addWidget(tile);
    }
    m_extractGridContainer->updateGeometry();
    
    switchView("图片提取");
    showNotification("图片已加载，调色板生成完毕");
}

void ColorPickerWindow::pasteImage() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime) return;

    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            processImage("", img);
            return;
        }
    }
    
    if (mime->hasUrls()) {
        QString path = mime->urls().first().toLocalFile();
        if (!path.isEmpty()) {
            QImage img(path);
            if (!img.isNull()) {
                processImage(path, img);
                return;
            }
        }
    }

    if (mime->hasFormat("image/png") || mime->hasFormat("image/jpeg") || mime->hasFormat("image/bmp")) {
        QImage img;
        if (img.loadFromData(mime->data("image/png"), "PNG") || 
            img.loadFromData(mime->data("image/jpeg"), "JPG") ||
            img.loadFromData(mime->data("image/bmp"), "BMP")) {
            processImage("", img);
            return;
        }
    }

    showNotification("剪贴板中没有图片或格式不支持", true);
}

QStringList ColorPickerWindow::extractDominantColors(const QImage& img, int num) {
    // 2026-03-xx 按照用户要求：将变量名 small 修改为 scaledImg，规避 Windows SDK 的 #define small char 冲突
    QImage scaledImg = img.scaled(120, 120, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    QMap<QRgb, int> counts;
    for (int y = 0; y < scaledImg.height(); ++y) {
        for (int x = 0; x < scaledImg.width(); ++x) { counts[scaledImg.pixel(x, y)]++; }
    }
    QList<QRgb> sorted = counts.keys();
    std::sort(sorted.begin(), sorted.end(), [&](QRgb a, QRgb b){ return counts[a] > counts[b]; });
    
    QStringList result;
    for (QRgb rgb : sorted) {
        QColor c(rgb);
        bool distinct = true;
        for(const QString& ex : result) {
            QColor exc(ex);
            int diff = abs(exc.red() - c.red()) + abs(exc.green() - c.green()) + abs(exc.blue() - c.blue());
            if(diff < 20) { distinct = false; break; }
        }
        if(distinct) {
            result << c.name().toUpper(); 
            if (result.size() >= num) break;
        }
    }
    return result;
}

void ColorPickerWindow::showNotification(const QString& message, bool isError) {
    // [ULTIMATE FIX] 增加防抖，防止连续操作导致的通知堆叠
    static qint64 lastNotifyTime = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastNotifyTime < 700) return;
    lastNotifyTime = now;

    if (m_notification) {
        m_notification->hide();
        m_notification->deleteLater();
        m_notification = nullptr;
    }
    
    m_notification = new QFrame(this);
    m_notification->setObjectName("notification");
    m_notification->setStyleSheet(QString("QFrame#notification { background: %1; border-radius: 6px; border: 1px solid rgba(255,255,255,0.2); }")
        .arg(isError ? "#e74c3c" : "#2ecc71"));
    
    auto* l = new QHBoxLayout(m_notification);
    l->setContentsMargins(15, 8, 15, 8);
    l->setSpacing(10);
    
    auto* icon = new QLabel();
    icon->setPixmap(IconHelper::getIcon(isError ? "close" : "select", "#FFFFFF").pixmap(18, 18));
    icon->setStyleSheet("border: none; background: transparent;");
    l->addWidget(icon);
    
    auto* lbl = new QLabel(message);
    lbl->setStyleSheet("color: white; font-weight: bold; font-size: 13px; border: none; background: transparent;");
    l->addWidget(lbl);
    
    m_notification->adjustSize();
    m_notification->move(width()/2 - m_notification->width()/2, height() - 80);
    m_notification->show();
    m_notification->raise();
    
    // 2026-03-xx 按照用户要求：全软件反馈提示统一缩短为 700ms，拒绝磨叽
    QTimer::singleShot(700, m_notification, &QWidget::hide);
}

void ColorPickerWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) event->acceptProposedAction();
}

void ColorPickerWindow::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasImage()) {
        QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
        if (!img.isNull()) processImage("", img);
    } else if (event->mimeData()->hasUrls()) {
        processImage(event->mimeData()->urls().first().toLocalFile());
    }
}

void ColorPickerWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) pasteImage();
    else FramelessDialog::keyPressEvent(event);
}

void ColorPickerWindow::hideEvent(QHideEvent* event) {
    QList<QWidget*> overlays = findChildren<QWidget*>();
    for (auto* w : overlays) {
        if (qobject_cast<ScreenColorPickerOverlay*>(w) || qobject_cast<PixelRulerOverlay*>(w)) {
            w->close();
        }
    }
    FramelessDialog::hideEvent(event);
}

bool ColorPickerWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        }
    }
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text, 700);
            return true;
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    } else if (event->type() == QEvent::MouseMove) {
        // [FIX] 拦截色块的鼠标移动事件，防止冒泡到父窗口触发错误的拖拽逻辑导致窗口跳动
        if (!watched->property("color").toString().isEmpty()) {
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        QString color = watched->property("color").toString();
        if (!color.isEmpty()) {
            if (me->button() == Qt::LeftButton) {
                useColor(color);
                ClipboardMonitor::instance().forceNext();
                QApplication::clipboard()->setText(color);
                showNotification("已应用并复制 " + color);
                return true;
            } else if (me->button() == Qt::RightButton) {
                // 右键弹出菜单，包含“从收藏中移除”
                showColorContextMenu(color, me->globalPosition().toPoint());
                return true;
            }
        } else if (watched == m_colorLabel) {
            if (me->button() == Qt::LeftButton) {
                // 根据用户要求：主预览标签点击不再自动复制，仅作展示
            } else if (me->button() == Qt::RightButton) {
                showColorContextMenu(m_currentColor, me->globalPosition().toPoint());
            }
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void ColorPickerWindow::showColorContextMenu(const QString& colorHex, const QPoint& globalPos) {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 20px 6px 10px; border-radius: 3px; } "
                       "QMenu::item:selected { background-color: #3e3e42; color: white; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

    menu.addAction(IconHelper::getIcon("copy", "#1abc9c", 18), "复制 HEX 代码", [this, colorHex]() {
        ClipboardMonitor::instance().forceNext();
        QApplication::clipboard()->setText(colorHex);
        showNotification("已复制 HEX: " + colorHex);
    });

    QColor c(colorHex);
    QString rgb = QString("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    menu.addAction(IconHelper::getIcon("copy", "#3498db", 18), "复制 RGB 代码", [this, rgb]() {
        ClipboardMonitor::instance().forceNext();
        QApplication::clipboard()->setText(rgb);
        showNotification("已复制 RGB: " + rgb);
    });

    if (m_favorites.contains(colorHex)) {
        menu.addAction(IconHelper::getIcon("close", "#e74c3c", 18), "从收藏中移除", [this, colorHex]() {
            removeFavorite(colorHex);
        });
    } else {
        menu.addAction(IconHelper::getIcon("Favorite", "#f1c40f", 18), "收藏此颜色", [this, colorHex]() {
            addSpecificColorToFavorites(colorHex);
        });
    }

    if (!m_currentImagePath.isEmpty() && m_stack->currentWidget() == m_extractScroll) {
        menu.addSeparator();
        QString path = m_currentImagePath;
        
        menu.addAction(IconHelper::getIcon("link", "#9b59b6", 18), "复制图片路径", [this, path]() {
            ClipboardMonitor::instance().forceNext();
            QApplication::clipboard()->setText(path);
            showNotification("已复制路径");
        });

        menu.addAction(IconHelper::getIcon("file", "#34495e", 18), "复制图片文件", [path]() {
            QMimeData* data = new QMimeData;
            QList<QUrl> urls;
            urls << QUrl::fromLocalFile(path);
            data->setUrls(urls);
            ClipboardMonitor::instance().forceNext();
            QApplication::clipboard()->setMimeData(data);
        });

        menu.addAction(IconHelper::getIcon("search", "#e67e22", 18), "定位图片文件", [path]() {
            QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(path) });
        });

        menu.addAction(IconHelper::getIcon("folder", "#f39c12", 18), "定位文件夹", [path]() {
            QFileInfo fi(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        });
    }

    menu.exec(globalPos);
}

QString ColorPickerWindow::rgbToHex(int r, int g, int b) { return QColor(r, g, b).name().toUpper(); }
QColor ColorPickerWindow::hexToColor(const QString& hex) { return QColor(hex); }
QString ColorPickerWindow::colorToHex(const QColor& c) { return c.name().toUpper(); }

QStringList ColorPickerWindow::loadFavorites() {
    QSettings s("RapidNotes", "ColorPicker");
    return s.value("favorites").toStringList();
}

void ColorPickerWindow::saveFavorites() {
    QSettings s("RapidNotes", "ColorPicker");
    s.setValue("favorites", m_favorites);
}

#include "ColorPickerWindow.moc"
