#include "ToolTipOverlay.h"
#include "ScreenshotTool.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QApplication>
#include <QScreen>
#include <QPainterPathStroker>
#include <QWheelEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QFileDialog>
#include <QClipboard>
#include <QMenu>
#include <QWidgetAction>
#include <utility>
#include <algorithm>
#include <QComboBox>
#include <QFontComboBox>
#include <QDateTime>
#include <QInputDialog>
#include <QFontMetrics>
#include <QStyle>
#include <QStyleOption>
#include <QColorDialog>
#include <QSettings>
#include <QAbstractItemView>
#include <QTreeView>
#include <QListView>
#include <QTableView>
#include <QDir>
#include <QGraphicsDropShadowEffect>
#include <QCoreApplication>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <tchar.h>
#include <uiautomation.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

QRect getActualWindowRect(HWND hwnd) {
    RECT rect;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }
    GetWindowRect(hwnd, &rect);
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static QList<QColor> getStandardColorList() {
    return {
        QColor(255, 0, 0), QColor(255, 165, 0), QColor(255, 255, 0), QColor(0, 255, 0),
        QColor(0, 255, 255), QColor(0, 0, 255), QColor(128, 0, 128), QColor(0, 0, 0), QColor(255, 255, 255)
    };
}

class IconFactory {
public:
    static QIcon createArrowStyleIcon(ArrowStyle style) {
        // 增大图标画布，以适应无文本的宽型菜单
        QPixmap pix(120, 32);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        
        QPointF start(10, 16), end(110, 16);
        QPointF dir = end - start;
        double angle = std::atan2(dir.y(), dir.x());
        double len = 100.0;

        bool isOutline = (style == ArrowStyle::OutlineSingle || style == ArrowStyle::OutlineDouble || style == ArrowStyle::OutlineDot);
        
        if (style == ArrowStyle::SolidSingle || style == ArrowStyle::OutlineSingle) {
            double hLen = 22;
            double bWid = 10;
            double wLen = 18;
            double wWid = 3;
            QPointF unit_dir = dir / len;
            QPointF perp_dir(-unit_dir.y(), unit_dir.x());

            if (isOutline) {
                p.setPen(QPen(Qt::white, 2.0));
                p.setBrush(Qt::transparent);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
            }
            p.drawPolygon(QPolygonF() << end 
                << end - unit_dir * hLen + perp_dir * bWid
                << end - unit_dir * wLen + perp_dir * wWid
                << start
                << end - unit_dir * wLen - perp_dir * wWid
                << end - unit_dir * hLen - perp_dir * bWid);
        } else if (style == ArrowStyle::Thin) {
            p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(start, end);
            double headSize = 18;
            p.drawLine(end, end - QPointF(headSize * std::cos(angle - 0.5), headSize * std::sin(angle - 0.5)));
            p.drawLine(end, end - QPointF(headSize * std::cos(angle + 0.5), headSize * std::sin(angle + 0.5)));
        } else if (style == ArrowStyle::SolidDouble || style == ArrowStyle::OutlineDouble) {
            p.setPen(QPen(Qt::white, 2.0));
            if (isOutline) p.setBrush(Qt::transparent); else p.setBrush(Qt::white);
            auto drawH = [&](const QPointF& e, double ang) {
                QPointF du(std::cos(ang), std::sin(ang));
                QPointF dp(-du.y(), du.x());
                p.drawPolygon(QPolygonF() << e << e - du * 16 + dp * 8 << e - du * 13 + dp * 2 << e - du * 13 - dp * 2 << e - du * 16 - dp * 8);
            };
            p.drawLine(start + (dir/len)*12, end - (dir/len)*12);
            drawH(end, angle); drawH(start, angle + M_PI);
        } else if (style == ArrowStyle::SolidDot || style == ArrowStyle::OutlineDot) {
            p.setPen(QPen(Qt::white, 2.0));
            if (isOutline) p.setBrush(Qt::transparent); else p.setBrush(Qt::white);
            p.drawLine(start, end - (dir/len)*12);
            p.drawEllipse(start, 5, 5);
            QPointF du(std::cos(angle), std::sin(angle));
            QPointF dp(-du.y(), du.x());
            p.drawPolygon(QPolygonF() << end << end - du * 16 + dp * 8 << end - du * 13 + dp * 2 << end - du * 13 - dp * 2 << end - du * 16 - dp * 8);
        } else if (style == ArrowStyle::Dimension) {
            p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawLine(start, end);
            QPointF du(std::cos(angle), std::sin(angle));
            QPointF dp(-du.y(), du.x());
            p.drawLine(start + dp * 8, start - dp * 8);
            p.drawLine(end + dp * 8, end - dp * 8);
        }
        return QIcon(pix);
    }
};

PinnedScreenshotWidget::PinnedScreenshotWidget(const QPixmap& pixmap, const QRect& screenRect, QWidget* parent)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_pixmap(pixmap)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(pixmap.size() / pixmap.devicePixelRatio());
    move(screenRect.topLeft());
}

void PinnedScreenshotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(rect(), m_pixmap);
    p.setPen(QPen(QColor(0, 120, 255, 200), 2));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void PinnedScreenshotWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_dragPos = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void PinnedScreenshotWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) move(e->globalPosition().toPoint() - m_dragPos);
}

void PinnedScreenshotWidget::mouseDoubleClickEvent(QMouseEvent*) { close(); }
void PinnedScreenshotWidget::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.addAction("复制", [this](){ 
        ClipboardMonitor::instance().forceNext();
        QApplication::clipboard()->setPixmap(m_pixmap); 
    });
    menu.addAction("保存", [this](){
        QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QString f = QFileDialog::getSaveFileName(this, "保存截图", fileName, "PNG(*.png)");
        if(!f.isEmpty()) m_pixmap.save(f);
    });
    menu.addSeparator();
    menu.addAction("关闭", this, &QWidget::close);
    menu.exec(e->globalPos());
}

SelectionInfoBar::SelectionInfoBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(180, 28);
    hide();
}
void SelectionInfoBar::updateInfo(const QRect& rect) {
    m_text = QString("%1, %2 | %3 x %4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    update();
}
void SelectionInfoBar::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0, 0, 0, 200)); p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 4, 4);
    p.setPen(Qt::white); p.setFont(QFont("Arial", 9));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

ScreenshotToolbar::ScreenshotToolbar(ScreenshotTool* tool) 
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) 
{
    m_tool = tool;
    setObjectName("ScreenshotToolbar");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setMouseTracking(true);

    setStyleSheet(R"(
        #MainContainer { 
            background-color: #2D2D2D; 
            border: 1px solid rgba(255, 255, 255, 0.1); 
            border-radius: 12px; 
        }
        #ToolRow, #OptionWidget {
            background: transparent;
        }
        QPushButton { 
            background: transparent; 
            border: none; 
            border-radius: 4px; 
            padding: 4px; 
        }
        #MainContainer QPushButton:hover { background-color: #3e3e42; } // 2026-03-xx 统一悬停色
        #MainContainer QPushButton:checked { background-color: #3e3e42; } // 2026-03-xx 统一选中色
        #OptionWidget { 
            background: transparent; 
        }
        QPushButton[colorBtn="true"] { padding: 0px; border-radius: 2px; }
        QPushButton[sizeBtn="true"] { background-color: #777; border-radius: 50%; }
        QPushButton[sizeBtn="true"]:checked { background-color: #007ACC; }
        QPushButton[sizeBtn="true"]:checked { background-color: #007ACC; }
    )");
    setAttribute(Qt::WA_StyledBackground);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 6, 6, 6);
    outerLayout->setSpacing(0);
    outerLayout->setSizeConstraint(QLayout::SetFixedSize);

    QFrame* mainContainer = new QFrame(this);
    mainContainer->setObjectName("MainContainer");
    mainContainer->setAttribute(Qt::WA_StyledBackground);
    outerLayout->addWidget(mainContainer);

    auto* mainLayout = new QVBoxLayout(mainContainer);
    mainLayout->setContentsMargins(4, 2, 4, 2); mainLayout->setSpacing(0);
    // [CRITICAL] 设置尺寸约束为 SetFixedSize，确保工具栏在子部件隐藏时能自动收缩高度，防止出现多余背景 / Use SetFixedSize to ensure toolbar shrinks when options are hidden
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    
    QWidget* toolRow = new QWidget(mainContainer);
    toolRow->setObjectName("ToolRow");
    auto* layout = new QHBoxLayout(toolRow);
    layout->setContentsMargins(6, 4, 6, 4); layout->setSpacing(2);

    // 2026-04-xx 按照宪法 [第五定律]：统一修正 ToolTip 格式为“半角空格引导+全角括号”
    addToolButton(layout, ScreenshotToolType::Rect, "screenshot_rect", "矩形 （R）");
    addToolButton(layout, ScreenshotToolType::Ellipse, "screenshot_ellipse", "椭圆 （E）");
    addToolButton(layout, ScreenshotToolType::Arrow, "screenshot_arrow", "箭头 （A）");
    addToolButton(layout, ScreenshotToolType::Line, "screenshot_line", "直线 （L）");
    
    addToolButton(layout, ScreenshotToolType::Pen, "screenshot_pen", "画笔 （P）");
    addToolButton(layout, ScreenshotToolType::Marker, "screenshot_marker", "记号笔 （M）");
    addToolButton(layout, ScreenshotToolType::Text, "screenshot_text", "文字 （T）");
    addToolButton(layout, ScreenshotToolType::Mosaic, "screenshot_mosaic", "画笔马赛克 （Z）");
    addToolButton(layout, ScreenshotToolType::MosaicRect, "screenshot_rect", "矩形马赛克 （M）");
    addToolButton(layout, ScreenshotToolType::Eraser, "screenshot_eraser", "橡皮擦 （X）");
    addToolButton(layout, ScreenshotToolType::Picker, "screen_picker", "取色器 （C）");

    layout->addSpacing(8);

    QFrame* divider = new QFrame();
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setStyleSheet("background-color: #555;");
    divider->setFixedWidth(1);
    divider->setFixedHeight(20);
    layout->addWidget(divider);

    layout->addSpacing(8);
    
    addActionButton(layout, "undo", "撤销 （Ctrl + Z）", [tool]{ tool->undo(); });
    addActionButton(layout, "redo", "重做 （Ctrl + Shift + Z）", [tool]{ tool->redo(); });
    addActionButton(layout, "screenshot_pin", "置顶截图 （F）", [tool]{ tool->pin(); });
    addActionButton(layout, "screenshot_ocr", "截图取文 （O）", [tool]{ tool->executeOCR(); });
    addActionButton(layout, "screenshot_save", "保存", [tool]{ tool->save(); });
    addActionButton(layout, "screenshot_copy", "复制 （Ctrl + C）", [tool]{ tool->copyToClipboard(); });
    addActionButton(layout, "screenshot_close", "取消 （Esc）", [tool]{ tool->cancel(); }); 
    addActionButton(layout, "screenshot_confirm", "完成 （Enter）", [tool]{ tool->confirm(); });

    mainLayout->addWidget(toolRow);

    // [CRITICAL] 水平分割线，用于分隔工具行和选项行 / Horizontal divider between tool row and option row
    m_horizontalDivider = new QFrame();
    m_horizontalDivider->setFrameShape(QFrame::HLine);
    m_horizontalDivider->setFrameShadow(QFrame::Plain);
    m_horizontalDivider->setStyleSheet("background-color: #555;");
    m_horizontalDivider->setFixedHeight(1);
    m_horizontalDivider->setVisible(false);
    mainLayout->addWidget(m_horizontalDivider);

    createOptionWidget();
    mainLayout->addWidget(m_optionWidget);
}

void ScreenshotToolbar::addToolButton(QBoxLayout* layout, ScreenshotToolType type, const QString& iconName, const QString& tip) {
    auto* btn = new QPushButton();
    btn->setAttribute(Qt::WA_StyledBackground);
    btn->setIcon(IconHelper::getIcon(iconName)); btn->setIconSize(QSize(20, 20));
    // btn->setToolTip(tip);
    btn->setProperty("tooltipText", tip);
    btn->installEventFilter(this);
    btn->setCheckable(true); btn->setFixedSize(32, 32);
    layout->addWidget(btn); m_buttons[type] = btn;
    connect(btn, &QPushButton::clicked, [this, type]{ selectTool(type); });
}

void ScreenshotToolbar::addActionButton(QBoxLayout* layout, const QString& iconName, const QString& tip, std::function<void()> func) {
    auto* btn = new QPushButton();
    btn->setAttribute(Qt::WA_StyledBackground);
    btn->setIcon(IconHelper::getIcon(iconName)); btn->setIconSize(QSize(20, 20));
    // btn->setToolTip(tip);
    btn->setProperty("tooltipText", tip);
    btn->installEventFilter(this);
    btn->setFixedSize(32, 32);
    layout->addWidget(btn); connect(btn, &QPushButton::clicked, func);
}

void ScreenshotToolbar::createOptionWidget() {
    m_optionWidget = new QWidget; m_optionWidget->setObjectName("OptionWidget");
    m_optionWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QHBoxLayout(m_optionWidget); layout->setContentsMargins(8, 4, 8, 4); layout->setSpacing(4);

    // 1. 箭头样式按钮 (迁移至最左侧)
    m_arrowStyleBtn = new QPushButton(); m_arrowStyleBtn->setFixedSize(56, 24);
    updateArrowButtonIcon(m_tool->m_currentArrowStyle);
    // 2026-04-xx 按照宪法规范修正格式
    m_arrowStyleBtn->setProperty("tooltipText", "箭头样式 （W）");
    m_arrowStyleBtn->installEventFilter(this);
    connect(m_arrowStyleBtn, &QPushButton::clicked, this, &ScreenshotToolbar::showArrowMenu);
    layout->addWidget(m_arrowStyleBtn);

    // 2. 形状填充选项 (Rect/Ellipse)
    m_outlineBtn = new QPushButton();
    m_outlineBtn->setCheckable(true);
    m_outlineBtn->setFixedSize(24, 24);
    m_outlineBtn->setIcon(IconHelper::getIcon("screenshot_rect", "#ffffff"));
    // 2026-04-xx 按照宪法规范修正格式
    m_outlineBtn->setProperty("tooltipText", "虚心 （Hollow）");
    m_outlineBtn->installEventFilter(this);
    m_outlineBtn->setStyleSheet("QPushButton { border: 1px solid #555; border-radius: 4px; } QPushButton:checked { background-color: #3e3e42; border-color: #007ACC; }"); // 2026-03-xx 统一选中色
    
    m_solidBtn = new QPushButton();
    m_solidBtn->setCheckable(true);
    m_solidBtn->setFixedSize(24, 24);
    m_solidBtn->setIcon(IconHelper::getIcon("screenshot_fill", "#ffffff"));
    // 2026-04-xx 按照宪法规范修正格式
    m_solidBtn->setProperty("tooltipText", "实心 （Solid）");
    m_solidBtn->installEventFilter(this);
    m_solidBtn->setStyleSheet("QPushButton { border: 1px solid #555; border-radius: 4px; } QPushButton:checked { background-color: #3e3e42; border-color: #007ACC; }"); // 2026-03-xx 统一选中色

    auto* fillGroup = new QButtonGroup(this);
    fillGroup->addButton(m_outlineBtn);
    fillGroup->addButton(m_solidBtn);
    layout->addWidget(m_outlineBtn);
    layout->addWidget(m_solidBtn);
    if (m_tool->m_fillEnabled) m_solidBtn->setChecked(true); else m_outlineBtn->setChecked(true);
    connect(m_outlineBtn, &QPushButton::clicked, [this]{ m_tool->setFillEnabled(false); });
    connect(m_solidBtn, &QPushButton::clicked, [this]{ m_tool->setFillEnabled(true); });

    // 3. 文字选项 (Text) - 采用独立胶囊布局 (Independent capsule layout)
    m_textOptionWidget = new QWidget(m_optionWidget);
    m_textOptionWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_textOptionWidget->setStyleSheet("background: transparent; border: none;");
    auto* textOptionLayout = new QHBoxLayout(m_textOptionWidget);
    textOptionLayout->setContentsMargins(0, 0, 0, 0); textOptionLayout->setSpacing(4);

    auto createCapsule = [this](QWidget* content, int width = -1) {
        QWidget* capsule = new QWidget(m_textOptionWidget);
        capsule->setFixedHeight(28);
        if (width > 0) capsule->setFixedWidth(width);
        capsule->setAttribute(Qt::WA_StyledBackground);
        capsule->setStyleSheet("background-color: #3D3D3D; border-radius: 6px; border: none;");
        auto* l = new QHBoxLayout(capsule);
        l->setContentsMargins(4, 0, 4, 0);
        l->setSpacing(0);
        l->addWidget(content);
        return capsule;
    };

    m_boldBtn = new QPushButton(); m_boldBtn->setCheckable(true); m_boldBtn->setFixedSize(24, 24);
    m_boldBtn->setIcon(IconHelper::getIcon("bold", "#ffffff")); 
    // 2026-04-xx 按照宪法规范修正格式
    m_boldBtn->setProperty("tooltipText", "加粗 （Bold）");
    m_boldBtn->installEventFilter(this);
    m_boldBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background-color: #3e3e42; } QPushButton:checked { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停/选中色
    m_boldBtn->setChecked(m_tool->m_currentBold);
    connect(m_boldBtn, &QPushButton::toggled, [this](bool checked){ m_tool->setBold(checked); });
    textOptionLayout->addWidget(createCapsule(m_boldBtn, 32));

    m_italicBtn = new QPushButton(); m_italicBtn->setCheckable(true); m_italicBtn->setFixedSize(24, 24);
    m_italicBtn->setIcon(IconHelper::getIcon("italic", "#ffffff")); 
    // 2026-04-xx 按照宪法规范修正格式
    m_italicBtn->setProperty("tooltipText", "倾斜 （Italic）");
    m_italicBtn->installEventFilter(this);
    m_italicBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background-color: #3e3e42; } QPushButton:checked { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停/选中色
    m_italicBtn->setChecked(m_tool->m_currentItalic);
    connect(m_italicBtn, &QPushButton::toggled, [this](bool checked){ m_tool->setItalic(checked); });
    textOptionLayout->addWidget(createCapsule(m_italicBtn, 32));

    auto* fontCombo = new QComboBox(); fontCombo->addItems({"微软雅黑", "宋体", "黑体", "楷体", "Arial", "Consolas"});
    fontCombo->setCurrentText(m_tool->m_currentFontFamily); fontCombo->setFixedSize(100, 28);
    fontCombo->setAttribute(Qt::WA_StyledBackground);
    fontCombo->setAttribute(Qt::WA_TranslucentBackground);
    fontCombo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fontCombo->setStyleSheet(R"(
        QComboBox { background-color: #3D3D3D; color: white; border: 1px solid transparent; border-radius: 6px; padding-left: 8px; }
        QComboBox::drop-down { width: 0px; border: none; }
        QComboBox QAbstractItemView { 
            background-color: #3D3D3D; 
            color: white;
            selection-background-color: transparent;
            border: 1px solid #555; 
            border-radius: 6px; 
            outline: none;
            padding: 2px;
        }
        QComboBox QAbstractItemView::item { 
            height: 18px; 
            border-radius: 3px; 
            padding: 0px 4px; 
            margin: 1px 2px;
            color: white;
            border: none;
        }
        QComboBox QAbstractItemView::item:selected { 
            background-color: #007ACC; 
            color: white; 
        }
        QScrollBar:vertical { width: 0px; background: transparent; }
    )");
    connect(fontCombo, &QComboBox::currentTextChanged, [this](const QString& font){ m_tool->setFontFamily(font); });
    fontCombo->view()->window()->setAttribute(Qt::WA_TranslucentBackground);
    fontCombo->view()->window()->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    fontCombo->view()->setFixedWidth(100);
    textOptionLayout->addWidget(fontCombo);

    auto* sizeCombo = new QComboBox();
    for(int s : {10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48}) sizeCombo->addItem(QString::number(s), s);
    sizeCombo->setCurrentIndex(sizeCombo->findData(m_tool->m_currentFontSize)); sizeCombo->setFixedSize(50, 28);
    sizeCombo->setAttribute(Qt::WA_StyledBackground);
    sizeCombo->setAttribute(Qt::WA_TranslucentBackground);
    sizeCombo->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sizeCombo->setStyleSheet(R"(
        QComboBox { background-color: #3D3D3D; color: white; border: 1px solid transparent; border-radius: 6px; padding-left: 8px; }
        QComboBox::drop-down { width: 0px; border: none; }
        QComboBox QAbstractItemView { 
            background-color: #3D3D3D; 
            color: white;
            selection-background-color: transparent;
            border: 1px solid #555; 
            border-radius: 6px; 
            outline: none;
            padding: 2px;
        }
        QComboBox QAbstractItemView::item { 
            height: 18px; 
            border-radius: 3px; 
            padding: 0px 4px; 
            margin: 1px 2px;
            color: white;
            border: none;
        }
        QComboBox QAbstractItemView::item:selected { 
            background-color: #007ACC; 
            color: white; 
        }
        QScrollBar:vertical { width: 0px; background: transparent; }
    )");
    connect(sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, sizeCombo](int index){ m_tool->setFontSize(sizeCombo->itemData(index).toInt()); });
    sizeCombo->view()->window()->setAttribute(Qt::WA_TranslucentBackground);
    sizeCombo->view()->window()->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    sizeCombo->view()->setFixedWidth(50);
    textOptionLayout->addWidget(sizeCombo);
    
    layout->addWidget(m_textOptionWidget);

    // [CRITICAL] 垂直分割线，用于分隔工具选项和粗细选项 / Vertical divider between tool options and size options
    m_textDivider = new QFrame();
    m_textDivider->setFrameShape(QFrame::VLine);
    m_textDivider->setFrameShadow(QFrame::Plain);
    m_textDivider->setStyleSheet("background-color: #555;");
    m_textDivider->setFixedWidth(1);
    m_textDivider->setFixedHeight(16);
    layout->addSpacing(4);
    layout->addWidget(m_textDivider);
    layout->addSpacing(4);

    // 4. 描边粗细 (Size Buttons)
    int sizes[] = {2, 4, 8};
    m_sizeGroup = new QButtonGroup(this);
    for(int s : sizes) {
        auto* btn = new QPushButton; btn->setProperty("sizeBtn", true); btn->setFixedSize(14 + s, 14 + s);
        btn->setCheckable(true); if(s == m_tool->m_currentStrokeWidth) btn->setChecked(true);
        layout->addWidget(btn); m_sizeGroup->addButton(btn);
        connect(btn, &QPushButton::clicked, [this, s]{ m_tool->setDrawWidth(s); });
    }

    // 分隔符
    QFrame* colorDivider = new QFrame();
    colorDivider->setFrameShape(QFrame::VLine);
    colorDivider->setFrameShadow(QFrame::Plain);
    colorDivider->setStyleSheet("background-color: #555;");
    colorDivider->setFixedWidth(1);
    colorDivider->setFixedHeight(16);
    layout->addSpacing(4);
    layout->addWidget(colorDivider);
    layout->addSpacing(4);

    // 5. 标准颜色选择区域 (9色)
    m_colorGroup = new QButtonGroup(this);
    m_colorGroup->setExclusive(false); // 改为非独占，实现全覆盖高亮
    for(const auto& c : getStandardColorList()) {
        auto* btn = new QPushButton; btn->setProperty("colorBtn", true); btn->setProperty("colorValue", c); 
        btn->setFixedSize(20, 20); btn->setCheckable(true);
        // [CRITICAL] 采用“等色边框”方案：非选中时边框颜色与背景一致，选中时变为白色。
        // 此变量极其重要：padding: 0px 和 border-color: %1 共同解决了 QSS 渲染时父容器背景色露底（黑色背景）的问题。
        // [CRITICAL] This variable is extremely important: padding: 0px and border-color: %1 
        // solve the issue of parent container background bleeding through (black background issue).
        btn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid %1; border-radius: 2px; padding: 0px; } "
                                   "QPushButton:hover { background-color: #3e3e42; border-color: #3e3e42; } " // 2026-03-xx 统一悬停色
                                   "QPushButton:checked { background-color: %1; border-color: white; }").arg(c.name()));
        layout->addWidget(btn); m_colorGroup->addButton(btn);
        connect(btn, &QPushButton::clicked, [this, c]{ m_tool->setDrawColor(c); });
    }

    // 分隔符 (右侧)
    QFrame* colorDividerRight = new QFrame();
    colorDividerRight->setFrameShape(QFrame::VLine);
    colorDividerRight->setFrameShadow(QFrame::Plain);
    colorDividerRight->setStyleSheet("background-color: #555;");
    colorDividerRight->setFixedWidth(1);
    colorDividerRight->setFixedHeight(16);
    layout->addSpacing(4);
    layout->addWidget(colorDividerRight);
    layout->addSpacing(4);

    // 6. 最近颜色展示区 (填满红色方框区域)
    auto* recentContainer = new QWidget(m_optionWidget);
    recentContainer->setStyleSheet("background: transparent; border: none;");
    m_recentLayout = new QHBoxLayout(recentContainer);
    m_recentLayout->setContentsMargins(0, 0, 0, 0); m_recentLayout->setSpacing(4);
    layout->addWidget(recentContainer);

    QSettings settings("RapidNotes", "Screenshot");
    for (const QString& name : settings.value("recentColors").toStringList()) { addRecentColor(QColor(name), false); }

    // --- 弹性间距 ---
    layout->addStretch();

    // 7. 右侧控制按钮统一管理
    // 色轮按钮 (图标换成调色盘)
    m_wheelBtn = new QPushButton(); m_wheelBtn->setFixedSize(32, 32);
    // [CRITICAL] 调色盘图标内部已设为彩色。此处传入当前颜色仅为染其外轮廓，以指示当前选色。
    // [CRITICAL] The palette icon now has fixed internal colors. Tinting here only affects the outline to indicate selection.
    m_wheelBtn->setIcon(IconHelper::getIcon("palette", m_tool->m_currentColor.name()));
    m_wheelBtn->setIconSize(QSize(20, 20));
    // 2026-04-xx 按照宪法规范修正格式
    m_wheelBtn->setProperty("tooltipText", "自定义颜色 （C）");
    m_wheelBtn->installEventFilter(this);
    connect(m_wheelBtn, &QPushButton::clicked, [this]{
        QColorDialog dialog(m_tool->m_currentColor, m_tool);
        dialog.setWindowTitle("选择标注颜色"); dialog.setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
        dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowStaysOnTopHint);
        if (dialog.exec() == QDialog::Accepted) { QColor c = dialog.selectedColor(); m_tool->setDrawColor(c); addRecentColor(c); }
        m_tool->activateWindow(); m_tool->updateToolbarPosition();
    });
    layout->addWidget(m_wheelBtn);

    // 调色盘 (图标换成金色五角星)
    m_paletteBtn = new QPushButton(); m_paletteBtn->setFixedSize(32, 32);
    // 2026-05-03 按照用户要求：修正图标调用错误，将 Favorite_filled 改为 star_filled
    m_paletteBtn->setIcon(IconHelper::getIcon("star_filled", "#FFD700"));
    m_paletteBtn->setIconSize(QSize(20, 20));
    // 2026-04-xx 按照宪法规范修正格式
    m_paletteBtn->setProperty("tooltipText", "颜色收藏夹 （G）");
    m_paletteBtn->installEventFilter(this);
    m_paletteBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_paletteBtn);

    QList<QColor> fullColors = {
        QColor(255, 0, 0), QColor(255, 69, 0), QColor(255, 165, 0), QColor(255, 215, 0),
        QColor(255, 255, 0), QColor(154, 205, 50), QColor(0, 255, 0), QColor(34, 139, 34),
        QColor(0, 255, 255), QColor(0, 191, 255), QColor(0, 120, 255), QColor(0, 0, 255),
        QColor(138, 43, 226), QColor(128, 0, 128), QColor(255, 0, 255), QColor(255, 192, 203),
        QColor(255, 255, 255), QColor(192, 192, 192), QColor(128, 128, 128), QColor(0, 0, 0)
    };
    connect(m_paletteBtn, &QPushButton::clicked, [this, fullColors]{
        QMenu* menu = new QMenu(this);
        IconHelper::setupMenu(menu);
        menu->setStyleSheet("QMenu { background-color: #2D2D2D; border: 1px solid #555; padding: 4px; }");
        QWidget* gridContainer = new QWidget; QGridLayout* grid = new QGridLayout(gridContainer);
        grid->setContentsMargins(4, 4, 4, 4); grid->setSpacing(4);
        int row = 0, col = 0;
        for (const auto& c : fullColors) {
            auto* btn = new QPushButton; btn->setFixedSize(22, 22);
            btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555; border-radius: 2px;").arg(c.name()));
            grid->addWidget(btn, row, col);
            connect(btn, &QPushButton::clicked, [this, c, menu]{ m_tool->setDrawColor(c); menu->close(); });
            if (++col >= 5) { col = 0; row++; }
        }
        QWidgetAction* action = new QWidgetAction(menu); action->setDefaultWidget(gridContainer); menu->addAction(action);
        menu->exec(QCursor::pos()); delete menu;
    });

    // 移除按钮
    m_removeColorBtn = new QPushButton("×");
    m_removeColorBtn->setFixedSize(18, 24); 
    // m_removeColorBtn->setToolTip("移除选中的最近颜色");
    m_removeColorBtn->setProperty("tooltipText", "移除选中的最近颜色");
    m_removeColorBtn->installEventFilter(this);
    m_removeColorBtn->setCursor(Qt::PointingHandCursor);
    m_removeColorBtn->setStyleSheet(R"(
        QPushButton { color: #999; background: transparent; border: none; font-size: 16px; font-weight: bold; }
        QPushButton:hover { color: #ff5555; }
    )");
    connect(m_removeColorBtn, &QPushButton::clicked, [this]{
        QColor current = m_tool->m_currentColor;
        for (int i = 0; i < m_recentLayout->count(); ++i) {
            auto* item = m_recentLayout->itemAt(i); auto* btn = qobject_cast<QPushButton*>(item->widget());
            if (btn && btn->property("colorValue").value<QColor>().rgba() == current.rgba()) {
                m_recentLayout->removeItem(item); btn->deleteLater(); delete item;
                QSettings settings("RapidNotes", "Screenshot"); QStringList recent = settings.value("recentColors").toStringList();
                recent.removeAll(current.name()); settings.setValue("recentColors", recent);
                break;
            }
        }
    });
    layout->addWidget(m_removeColorBtn);

    m_optionWidget->setVisible(false);
    syncColorSelection(m_tool->m_currentColor);
}

void ScreenshotToolbar::showArrowMenu() {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    // 使用 QWidgetAction 替代 QMenu::setIconSize 以获得更好的版本兼容性和视觉控制
    menu.setStyleSheet(R"(
        QMenu { background-color: #2D2D2D; border: 1px solid #555; padding: 2px; }
        QMenu::item { padding: 0px; }
    )");

    auto addAct = [&](ArrowStyle s) {
        QWidgetAction* action = new QWidgetAction(&menu);
        QPushButton* btn = new QPushButton();
        btn->setFixedSize(120, 36);
        btn->setIcon(IconFactory::createArrowStyleIcon(s));
        btn->setIconSize(QSize(100, 24));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(R"(
            QPushButton { background-color: transparent; border: none; border-radius: 4px; }
            QPushButton:hover { background-color: #3e3e42; } // 2026-03-xx 统一悬停色
        )");
        connect(btn, &QPushButton::clicked, [this, s, &menu]{ 
            m_tool->setArrowStyle(s); 
            updateArrowButtonIcon(s); 
            menu.close(); 
        });
        action->setDefaultWidget(btn);
        menu.addAction(action);
    };

    addAct(ArrowStyle::SolidSingle);
    addAct(ArrowStyle::SolidDouble);
    addAct(ArrowStyle::OutlineSingle);
    addAct(ArrowStyle::OutlineDouble);
    addAct(ArrowStyle::Dimension);
    addAct(ArrowStyle::Thin);
    addAct(ArrowStyle::SolidDot);
    addAct(ArrowStyle::OutlineDot);

    menu.exec(mapToGlobal(m_arrowStyleBtn->geometry().bottomLeft()));
}

void ScreenshotToolbar::addRecentColor(const QColor& c, bool save) {
    if (!m_recentLayout || !m_wheelBtn) return;
    
    // 1. 优先同步高亮状态 (Ensure highlight is synced even if not added to recent)
    syncColorSelection(c);

    // 2. 如果是 9 种标准色之一，则不重复添加到“最近颜色”
    for (const auto& sc : getStandardColorList()) {
        if (sc.rgba() == c.rgba()) return;
    }

    // 3. 检查是否已经在最近颜色列表中
    for (int i = 0; i < m_recentLayout->count(); ++i) {
        auto* b = qobject_cast<QPushButton*>(m_recentLayout->itemAt(i)->widget());
        if (b && b->property("colorValue").value<QColor>().rgba() == c.rgba()) return;
    }

    // 4. 限制最近颜色数量 (增加到 15 以填满区域)
    if (m_recentLayout->count() >= 15) {
        auto* item = m_recentLayout->takeAt(0);
        if (item->widget()) {
            if (m_colorGroup) m_colorGroup->removeButton(qobject_cast<QPushButton*>(item->widget()));
            item->widget()->deleteLater();
        }
        delete item;
    }

    // 5. 创建并添加新的最近颜色按钮
    auto* btn = new QPushButton; btn->setFixedSize(20, 20);
    btn->setProperty("colorBtn", true);
    btn->setProperty("colorValue", c);
    btn->setCheckable(true);
    // [CRITICAL] 采用与标准色一致的“等色边框”方案，这是解决颜色按钮黑色背景问题的核心逻辑。
    // [CRITICAL] This logic is the core fix for the color button's black background issue.
    btn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid %1; border-radius: 2px; padding: 0px; } "
                               "QPushButton:hover { background-color: #3e3e42; border-color: #3e3e42; } " // 2026-03-xx 统一悬停色
                               "QPushButton:checked { background-color: %1; border-color: white; }").arg(c.name()));
    // btn->setToolTip(c.name());
    btn->setProperty("tooltipText", c.name());
    btn->installEventFilter(this);
    m_recentLayout->addWidget(btn);
    if (m_colorGroup) m_colorGroup->addButton(btn);

    connect(btn, &QPushButton::clicked, [this, c]{
        m_tool->setDrawColor(c);
    });

    if (save) {
        QSettings settings("RapidNotes", "Screenshot");
        QStringList recent = settings.value("recentColors").toStringList();
        if (!recent.contains(c.name())) {
            recent.append(c.name());
            if (recent.size() > 15) recent.removeFirst();
            settings.setValue("recentColors", recent);
        }
    }
    
    // 再次调用以确保新添加的按钮也被正确选中
    syncColorSelection(c);
}

void ScreenshotToolbar::syncColorSelection(const QColor& color) {
    if (!m_colorGroup) return;
    
    // 暂时阻塞信号，避免循环触发
    m_colorGroup->blockSignals(true);
    // m_colorGroup 包含标准色和最近色按钮，实现全覆盖同步 (Cover both standard and recent colors)
    for (auto* btn : m_colorGroup->buttons()) {
        QColor btnColor = btn->property("colorValue").value<QColor>();
        // 使用 rgba 进行精确对比，实现全覆盖高亮 (All coverage highlight)
        if (btnColor.rgba() == color.rgba()) {
            btn->setChecked(true);
        } else {
            btn->setChecked(false);
        }
    }
    
    // [CRITICAL] 同步更新调色盘图标的外轮廓颜色，而内部色点保持彩色。
    if (m_wheelBtn) {
        m_wheelBtn->setIcon(IconHelper::getIcon("palette", color.name()));
    }
    
    m_colorGroup->blockSignals(false);
}

void ScreenshotToolbar::updateArrowButtonIcon(ArrowStyle style) {
    m_arrowStyleBtn->setIcon(IconFactory::createArrowStyleIcon(style)); 
    m_arrowStyleBtn->setIconSize(QSize(48, 16));
}

void ScreenshotToolbar::selectTool(ScreenshotToolType type) {
    for(auto* b : std::as_const(m_buttons)) b->setChecked(false);
    if(m_buttons.contains(type)) m_buttons[type]->setChecked(true);
    
    // 恢复原来的两行逻辑：显示就展示选项卡
    bool hasOptions = (type != ScreenshotToolType::None);
    m_optionWidget->setVisible(hasOptions);
    if (m_horizontalDivider) m_horizontalDivider->setVisible(hasOptions);

    bool isArrow = (type == ScreenshotToolType::Arrow);
    bool isRectOrEllipse = (type == ScreenshotToolType::Rect || type == ScreenshotToolType::Ellipse);
    bool isText = (type == ScreenshotToolType::Text);

    m_arrowStyleBtn->setVisible(isArrow);
    m_outlineBtn->setVisible(isRectOrEllipse);
    m_solidBtn->setVisible(isRectOrEllipse);
    m_textOptionWidget->setVisible(isText);

    if (m_textDivider) {
        m_textDivider->setVisible(isArrow || isRectOrEllipse || isText);
    }

    m_tool->setTool(type); 
    
    // [CRITICAL] 必须调用此系列方法以强制触发窗口尺寸重新计算，消除高度异常 / Force UI update to recalculate size
    m_optionWidget->updateGeometry();
    if (m_horizontalDivider) m_horizontalDivider->updateGeometry();
    if (layout()) layout()->activate();
    adjustSize(); 
    m_tool->updateToolbarPosition();
}

void ScreenshotToolbar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true; m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}
void ScreenshotToolbar::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging) move(event->globalPosition().toPoint() - m_dragPosition);
}
void ScreenshotToolbar::mouseReleaseEvent(QMouseEvent *) { m_isDragging = false; }
void ScreenshotToolbar::paintEvent(QPaintEvent *) {
    // 作为透明容器，不再执行默认绘制逻辑，完全由 MainContainer 处理背景
}

void ScreenshotToolbar::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 截图工具条按 Esc 不直接关闭，统一由 ScreenshotTool 控制
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

bool ScreenshotToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::HoverLeave) {
        ToolTipOverlay::hideTip();
    }
    return QWidget::eventFilter(watched, event);
}

ScreenshotTool::~ScreenshotTool() {
    if (m_toolbar) m_toolbar->deleteLater();
    qDeleteAll(m_annotations);
    qDeleteAll(m_redoStack);
    if (m_activeShape) {
        delete m_activeShape;
        m_activeShape = nullptr;
    }
}

ScreenshotTool::ScreenshotTool(QWidget* parent) 
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowState(Qt::WindowFullScreen);
    setMouseTracking(true);
    m_screenPixmap = QGuiApplication::primaryScreen()->grabWindow(0);
    m_screenImage = m_screenPixmap.toImage();
    // [OPTIMIZATION] 移除初始化时的全屏马赛克生成，改为“按需延迟生成”以节省显存 (约节省 130MB+ / 4K屏)
    m_mosaicPixmap = QPixmap();

    QSettings settings("RapidNotes", "Screenshot");
    m_currentColor = settings.value("color", QColor(255, 50, 50)).value<QColor>();
    m_currentStrokeWidth = settings.value("strokeWidth", 3).toInt();
    m_currentArrowStyle = static_cast<ArrowStyle>(settings.value("arrowStyle", 0).toInt());
    m_currentTool = static_cast<ScreenshotToolType>(settings.value("tool", 0).toInt());
    m_currentFontFamily = settings.value("fontFamily", "Microsoft YaHei").toString();
    m_currentFontSize = settings.value("fontSize", 14).toInt();
    m_currentBold = settings.value("bold", true).toBool();
    m_currentItalic = settings.value("italic", false).toBool();
    m_isConfirmed = false;

    m_toolbar = new ScreenshotToolbar(this); m_toolbar->hide();
    m_infoBar = new SelectionInfoBar(this);
    m_lastMouseMovePos = mapFromGlobal(QCursor::pos());
    m_textInput = new QLineEdit(this); m_textInput->hide(); m_textInput->setFrame(false);
    m_textInput->installEventFilter(this);
    connect(m_textInput, &QLineEdit::editingFinished, this, &ScreenshotTool::commitTextInput);
}

void ScreenshotTool::showEvent(QShowEvent* event) { QWidget::showEvent(event); detectWindows(); }
void ScreenshotTool::cancel() { 
    emit screenshotCanceled(); 
    if (m_toolbar) m_toolbar->close(); 
    // [OPTIMIZATION] 退出时显式释放巨大全屏资源，防止 deleteLater 延迟析构导致的内存堆叠
    m_screenPixmap = QPixmap();
    m_screenImage = QImage();
    m_mosaicPixmap = QPixmap();
    close(); 
}

int BaseShape::getHandleAt(const QPoint& pos) const {
    auto handles = getHandles();
    for (int i = 0; i < handles.size(); ++i) {
        if (handles[i].contains(pos)) return i;
    }
    return -1;
}

void BaseShape::moveBy(const QPoint& delta) {
    for (auto& p : data.points) p += delta;
}

static bool isNearLine(const QPointF& p, const QPointF& s, const QPointF& e, int threshold) {
    double l2 = QPointF::dotProduct(e - s, e - s);
    if (l2 == 0.0) return (p - s).manhattanLength() < threshold;
    double t = std::max(0.0, std::min(1.0, QPointF::dotProduct(p - s, e - s) / l2));
    QPointF projection = s + t * (e - s);
    return (p - projection).manhattanLength() < threshold;
}

void RectShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.size() < 2) return;
    p.setPen(QPen(data.color, data.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(data.isFilled ? QBrush(data.color) : Qt::NoBrush);
    p.drawRect(QRectF(data.points[0], data.points[1]).normalized());
}

bool RectShape::hitTest(const QPoint& pos) const {
    if (data.points.size() < 2) return false;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    if (data.isFilled && r.contains(pos)) return true;
    int t = std::max(5, data.strokeWidth + 2);
    return std::abs(pos.x() - r.left()) < t || std::abs(pos.x() - r.right()) < t ||
           std::abs(pos.y() - r.top()) < t || std::abs(pos.y() - r.bottom()) < t;
}

QList<QRect> RectShape::getHandles() const {
    if (data.points.size() < 2) return {};
    QList<QRect> h; int s = 10;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    h << QRect(r.left()-s/2, r.top()-s/2, s, s) << QRect(r.right()-s/2, r.top()-s/2, s, s)
      << QRect(r.right()-s/2, r.bottom()-s/2, s, s) << QRect(r.left()-s/2, r.bottom()-s/2, s, s);
    return h;
}

void RectShape::updatePoint(int index, const QPoint& pos) {
    if (data.points.size() < 2) return;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    if (index == 0) { r.setTopLeft(pos); }
    else if (index == 1) { r.setTopRight(pos); }
    else if (index == 2) { r.setBottomRight(pos); }
    else if (index == 3) { r.setBottomLeft(pos); }
    data.points[0] = r.topLeft(); data.points[1] = r.bottomRight();
}

void EllipseShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.size() < 2) return;
    p.setPen(QPen(data.color, data.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(data.isFilled ? QBrush(data.color) : Qt::NoBrush);
    p.drawEllipse(QRectF(data.points[0], data.points[1]).normalized());
}

bool EllipseShape::hitTest(const QPoint& pos) const {
    if (data.points.size() < 2) return false;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    QPainterPath path; path.addEllipse(r);
    if (data.isFilled && path.contains(pos)) return true;
    QPainterPathStroker stroker; stroker.setWidth(std::max(10, data.strokeWidth + 4));
    return stroker.createStroke(path).contains(pos);
}

QList<QRect> EllipseShape::getHandles() const {
    if (data.points.size() < 2) return {};
    QList<QRect> h; int s = 10;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    h << QRect(r.left()-s/2, r.top()-s/2, s, s) << QRect(r.right()-s/2, r.top()-s/2, s, s)
      << QRect(r.right()-s/2, r.bottom()-s/2, s, s) << QRect(r.left()-s/2, r.bottom()-s/2, s, s);
    return h;
}

void EllipseShape::updatePoint(int index, const QPoint& pos) {
    if (data.points.size() < 2) return;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    if (index == 0) { r.setTopLeft(pos); }
    else if (index == 1) { r.setTopRight(pos); }
    else if (index == 2) { r.setBottomRight(pos); }
    else if (index == 3) { r.setBottomLeft(pos); }
    data.points[0] = r.topLeft(); data.points[1] = r.bottomRight();
}

void LineShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.size() < 2) return;
    p.setPen(QPen(data.color, data.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(data.points[0], data.points[1]);
}

bool LineShape::hitTest(const QPoint& pos) const {
    if (data.points.size() < 2) return false;
    return isNearLine(pos, data.points[0], data.points[1], std::max(8, data.strokeWidth + 3));
}

QList<QRect> LineShape::getHandles() const {
    if (data.points.size() < 2) return {};
    QList<QRect> h; int s = 10;
    h << QRect(data.points[0].toPoint().x()-s/2, data.points[0].toPoint().y()-s/2, s, s)
      << QRect(data.points[1].toPoint().x()-s/2, data.points[1].toPoint().y()-s/2, s, s);
    return h;
}

void ArrowShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.size() < 2) return;
    QPointF start = data.points[0], end = data.points[1];
    QPointF dir = end - start;
    double len = std::sqrt(QPointF::dotProduct(dir, dir));
    if (len < 2) return;
    QPointF unit = dir / len; QPointF perp(-unit.y(), unit.x());
    double angle = std::atan2(dir.y(), dir.x());
    
    // 增加基础尺寸，使其更显眼
    double baseSize = 24 + data.strokeWidth * 2.0;
    double headLen = baseSize;
    bool isOutline = (data.arrowStyle == ArrowStyle::OutlineSingle || data.arrowStyle == ArrowStyle::OutlineDouble || data.arrowStyle == ArrowStyle::OutlineDot);
    
    if (data.arrowStyle == ArrowStyle::SolidSingle || data.arrowStyle == ArrowStyle::OutlineSingle) {
        if (isOutline) {
            QPointF neck = end - unit * (headLen * 0.8); double w = data.strokeWidth + 2;
            QPolygonF poly; poly << end << neck + perp * (headLen*0.5) << neck + perp * w << start + perp * w << start - perp * w << neck - perp * w << neck - perp * (headLen*0.5);
            p.setBrush(Qt::transparent); p.setPen(QPen(data.color, 2.5)); p.drawPolygon(poly);
        } else {
            double barbWidth = headLen * 0.55; double waistLen = headLen * 0.75; double waistWidth = data.strokeWidth + 1;
            p.setPen(Qt::NoPen); p.setBrush(data.color);
            p.drawPolygon(QPolygonF() << end << end - unit * headLen + perp * barbWidth << end - unit * waistLen + perp * waistWidth << start << end - unit * waistLen - perp * waistWidth << end - unit * headLen - perp * barbWidth);
        }
    } else if (data.arrowStyle == ArrowStyle::Thin) {
        p.setPen(QPen(data.color, data.strokeWidth + 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(start, end);
        double thinAngle = 0.5; double thinLen = headLen * 0.8;
        p.drawLine(end, end - QPointF(thinLen * std::cos(angle - thinAngle), thinLen * std::sin(angle - thinAngle)));
        p.drawLine(end, end - QPointF(thinLen * std::cos(angle + thinAngle), thinLen * std::sin(angle + thinAngle)));
    } else if (data.arrowStyle == ArrowStyle::SolidDouble || data.arrowStyle == ArrowStyle::OutlineDouble) {
        p.setPen(QPen(data.color, data.strokeWidth + 1)); p.drawLine(start + unit * (headLen*0.7), end - unit * (headLen*0.7));
        p.setPen(isOutline ? QPen(data.color, 2.0) : Qt::NoPen);
        p.setBrush(isOutline ? Qt::transparent : data.color);
        auto drawH = [&](const QPointF& e, double ang) {
            QPointF u(std::cos(ang), std::sin(ang)), pr(-u.y(), u.x());
            p.drawPolygon(QPolygonF() << e << e - u * headLen + pr * (headLen*0.5) << e - u * (headLen*0.7) + pr * (data.strokeWidth) << e - u * (headLen*0.7) - pr * (data.strokeWidth) << e - u * headLen - pr * (headLen*0.5));
        };
        drawH(end, angle); drawH(start, angle + M_PI);
    } else if (data.arrowStyle == ArrowStyle::SolidDot || data.arrowStyle == ArrowStyle::OutlineDot) {
        p.setPen(QPen(data.color, data.strokeWidth + 1)); p.drawLine(start, end - unit * (headLen*0.7));
        p.setPen(isOutline ? QPen(data.color, 2.0) : Qt::NoPen);
        p.setBrush(isOutline ? Qt::transparent : data.color);
        p.drawEllipse(start, 5+data.strokeWidth, 5+data.strokeWidth);
        p.drawPolygon(QPolygonF() << end << end - unit * headLen + perp * (headLen*0.5) << end - unit * (headLen*0.7) + perp * (data.strokeWidth) << end - unit * (headLen*0.7) - perp * (data.strokeWidth) << end - unit * headLen - perp * (headLen*0.5));
    } else if (data.arrowStyle == ArrowStyle::Dimension) {
        p.setPen(QPen(data.color, data.strokeWidth + 1, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawLine(start, end);
        p.drawLine(start + perp * (10 + data.strokeWidth), start - perp * (10 + data.strokeWidth));
        p.drawLine(end + perp * (10 + data.strokeWidth), end - perp * (10 + data.strokeWidth));
    }
}

bool ArrowShape::hitTest(const QPoint& pos) const {
    if (data.points.size() < 2) return false;
    return isNearLine(pos, data.points[0], data.points[1], std::max(15, data.strokeWidth + 8));
}

QList<QRect> ArrowShape::getHandles() const {
    if (data.points.size() < 2) return {};
    QList<QRect> h; int s = 10;
    h << QRect(data.points[0].toPoint().x()-s/2, data.points[0].toPoint().y()-s/2, s, s)
      << QRect(data.points[1].toPoint().x()-s/2, data.points[1].toPoint().y()-s/2, s, s);
    return h;
}

void PenShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.size() < 2) return;
    p.setPen(QPen(data.color, data.strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path; path.moveTo(data.points[0]);
    for(int i=1; i<data.points.size(); ++i) path.lineTo(data.points[i]);
    p.drawPath(path);
}

bool PenShape::hitTest(const QPoint& pos) const {
    for (int i = 0; i < data.points.size() - 1; ++i) {
        if (isNearLine(pos, data.points[i], data.points[i+1], std::max(10, data.strokeWidth + 4))) return true;
    }
    return false;
}

void MarkerShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.isEmpty()) return;
    p.setBrush(data.color); p.setPen(Qt::NoPen); int r = 12 + data.strokeWidth;
    p.drawEllipse(data.points[0], r, r); p.setPen(Qt::white); p.setFont(QFont("Arial", r, QFont::Bold));
    p.drawText(QRectF(data.points[0].x()-r, data.points[0].y()-r, r*2, r*2), Qt::AlignCenter, data.text);
}

bool MarkerShape::hitTest(const QPoint& pos) const {
    if (data.points.isEmpty()) return false;
    int r = 12 + data.strokeWidth;
    return (pos - data.points[0].toPoint()).manhattanLength() < r + 5;
}

void TextShape::draw(QPainter& p, const QPixmap&) const {
    if (data.points.isEmpty() || data.text.isEmpty()) return;
    p.setPen(data.color);
    QFont font(data.fontFamily, data.fontSize);
    font.setBold(data.isBold);
    font.setItalic(data.isItalic);
    p.setFont(font);
    p.drawText(data.points[0], data.text);
}

bool TextShape::hitTest(const QPoint& pos) const {
    if (data.points.isEmpty() || data.text.isEmpty()) return false;
    QFont font(data.fontFamily, data.fontSize);
    font.setBold(data.isBold); font.setItalic(data.isItalic);
    QRect r = QFontMetrics(font).boundingRect(data.text);
    r.moveTo(data.points[0].toPoint().x(), data.points[0].toPoint().y() - r.height());
    return r.adjusted(-10, -10, 10, 10).contains(pos);
}

QList<QRect> TextShape::getHandles() const {
    if (data.points.isEmpty() || data.text.isEmpty()) return {};
    QList<QRect> h; int s = 10;
    // 为文字左上角提供一个移动句柄
    h << QRect(data.points[0].toPoint().x()-s/2, data.points[0].toPoint().y()-s/2, s, s);
    return h;
}

void MosaicShape::draw(QPainter& p, const QPixmap& mosaicPixmap) const {
    if (data.points.size() < 2) return;
    p.save();
    QPainterPath path; path.moveTo(data.points[0]);
    for(int i=1; i<data.points.size(); ++i) path.lineTo(data.points[i]);
    QPainterPathStroker s; s.setWidth(data.strokeWidth * 6);
    p.setClipPath(s.createStroke(path));
    p.drawPixmap(0, 0, mosaicPixmap);
    p.restore();
}

bool MosaicShape::hitTest(const QPoint& pos) const {
    for (int i = 0; i < data.points.size() - 1; ++i) {
        if (isNearLine(pos, data.points[i], data.points[i+1], data.strokeWidth * 3)) return true;
    }
    return false;
}

void MosaicRectShape::draw(QPainter& p, const QPixmap& mosaicPixmap) const {
    if (data.points.size() < 2) return;
    p.save();
    p.setClipRect(QRectF(data.points[0], data.points[1]).normalized());
    p.drawPixmap(0, 0, mosaicPixmap);
    p.restore();
}

bool MosaicRectShape::hitTest(const QPoint& pos) const {
    if (data.points.size() < 2) return false;
    return QRectF(data.points[0], data.points[1]).normalized().contains(pos);
}

QList<QRect> MosaicRectShape::getHandles() const {
    if (data.points.size() < 2) return {};
    QList<QRect> h; int s = 10;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    h << QRect(r.left()-s/2, r.top()-s/2, s, s) << QRect(r.right()-s/2, r.top()-s/2, s, s)
      << QRect(r.right()-s/2, r.bottom()-s/2, s, s) << QRect(r.left()-s/2, r.bottom()-s/2, s, s);
    return h;
}

void MosaicRectShape::updatePoint(int index, const QPoint& pos) {
    if (data.points.size() < 2) return;
    QRect r = QRectF(data.points[0], data.points[1]).normalized().toRect();
    if (index == 0) { r.setTopLeft(pos); }
    else if (index == 1) { r.setTopRight(pos); }
    else if (index == 2) { r.setBottomRight(pos); }
    else if (index == 3) { r.setBottomLeft(pos); }
    data.points[0] = r.topLeft(); data.points[1] = r.bottomRight();
}

static BaseShape* createShape(const DrawingAnnotation& ann) {
    switch(ann.type) {
        case ScreenshotToolType::Rect: return new RectShape(ann);
        case ScreenshotToolType::Ellipse: return new EllipseShape(ann);
        case ScreenshotToolType::Line: return new LineShape(ann);
        case ScreenshotToolType::Arrow: return new ArrowShape(ann);
        case ScreenshotToolType::Pen: return new PenShape(ann);
        case ScreenshotToolType::Marker: return new MarkerShape(ann);
        case ScreenshotToolType::Text: return new TextShape(ann);
        case ScreenshotToolType::Mosaic: return new MosaicShape(ann);
        case ScreenshotToolType::MosaicRect: return new MosaicRectShape(ann);
        default: return nullptr;
    }
}

void ScreenshotTool::mousePressEvent(QMouseEvent* e) {
    m_lastMouseMovePos = e->pos();
    setFocus();
    if(m_textInput->isVisible() && !m_textInput->geometry().contains(e->pos())) commitTextInput();
    
    if (m_currentTool == ScreenshotToolType::Picker && e->button() == Qt::LeftButton) {
        QColor c = m_screenImage.pixelColor(e->pos());
        setDrawColor(c);
        // 取色后切回上一个工具，如果上一个是 None 则保持 None
        setTool(ScreenshotToolType::None);
        m_toolbar->selectTool(ScreenshotToolType::None);
        update();
        return;
    }
    if(e->button() == Qt::RightButton) {
        // [用户修改要求] 拦截右键按下，统一在 Release 中处理取消逻辑，防止事件穿透到第三方应用触发菜单
        e->accept();
        return;
    }
    if(e->button() != Qt::LeftButton) return;
    m_dragHandle = -1; m_editHandle = -1;

    // 强化：在按下鼠标时重新进行一次命中测试，确保 hoveredShape 准确
    if (m_state == ScreenshotState::Editing && !m_isDragging && !m_isDrawing) {
        m_hoveredShape = nullptr;
        for (int i = m_annotations.size() - 1; i >= 0; --i) {
            if (m_annotations[i]->hitTest(e->pos()) || m_annotations[i]->getHandleAt(e->pos()) != -1) {
                m_hoveredShape = m_annotations[i];
                break;
            }
        }
    }

    if (m_state == ScreenshotState::Selecting) {
        m_startPoint = e->pos(); m_endPoint = m_startPoint; m_isDragging = true; m_toolbar->hide(); m_infoBar->hide();
    } else {
        // 优先处理已有标注的句柄
        if (m_hoveredShape) {
            int handle = m_hoveredShape->getHandleAt(e->pos());
            // [CRITICAL] 使用 m_dragOrigin 而不是 m_startPoint，防止修改选区大小 / Use m_dragOrigin to prevent selection resize
            if (handle != -1) { m_editHandle = handle; m_dragOrigin = e->pos(); m_isDragging = true; update(); return; }
            if (m_hoveredShape->hitTest(e->pos())) { m_editHandle = 100; m_dragOrigin = e->pos(); m_isDragging = true; update(); return; }
        }

        int handle = getHandleAt(e->pos());
        if (selectionRect().contains(e->pos()) && m_currentTool != ScreenshotToolType::None && handle == -1) {
            if (m_currentTool == ScreenshotToolType::Text) { showTextInput(e->pos()); return; }
            m_isDrawing = true; m_currentAnnotation = {m_currentTool, {e->pos()}, m_currentColor, "", m_currentStrokeWidth, LineStyle::Solid, m_currentArrowStyle, m_fillEnabled, 
                                                       m_currentFontFamily, m_currentFontSize, m_currentBold, m_currentItalic};
            if(m_currentTool == ScreenshotToolType::Marker) {
                int c = 1; for(auto* a : std::as_const(m_annotations)) if(a->data.type == ScreenshotToolType::Marker) c++;
                m_currentAnnotation.text = QString::number(c);
            }
            if (m_activeShape) {
                delete m_activeShape;
                m_activeShape = nullptr;
            }
            m_activeShape = createShape(m_currentAnnotation);
        } else if (handle != -1) {
            m_dragHandle = handle; m_isDragging = true;
        } else if (selectionRect().contains(e->pos())) {
            // [CRITICAL] 移动选区时使用 m_dragOrigin / Use m_dragOrigin when moving selection
            m_dragHandle = 8; m_dragOrigin = e->pos(); m_isDragging = true;
        }
    }
    update();
}

void ScreenshotTool::mouseMoveEvent(QMouseEvent* e) {
    m_lastMouseMovePos = e->pos();
    if (m_state == ScreenshotState::Selecting && !m_isDragging) {
        QRect smallest; long long minArea = -1;
        QPoint p = e->pos();
        
        // 2026-03-xx 改进算法：在包含鼠标点的矩形中，寻找面积最小的那个。
        // 同时引入“宽高比”保护，防止选中极其细长的线条控件。
        for (const QRect& r : std::as_const(m_detectedRects)) {
            if (r.contains(p)) {
                // 过滤掉面积过小的杂质
                if (r.width() < 10 || r.height() < 10) continue;
                
                long long area = (long long)r.width() * r.height();
                if (minArea == -1 || area < minArea) {
                    minArea = area;
                    smallest = r;
                }
            }
        }
        
        // 增加逻辑：如果找到了最小矩形，但其面积占据了全屏的 95% 以上，通常意味着选中的是桌面背景，
        // 此时我们尝试寻找更具体的子项，或者保持原样。
        
        if (m_highlightedRect != smallest) {
            m_highlightedRect = smallest;
            update();
        }
    } else {
        m_highlightedRect = QRect();
    }

    if (!m_isDragging && !m_isDrawing && m_state == ScreenshotState::Editing) {
        BaseShape* prevHover = m_hoveredShape; m_hoveredShape = nullptr;
        for (int i = m_annotations.size() - 1; i >= 0; --i) {
            if (m_annotations[i]->hitTest(e->pos()) || m_annotations[i]->getHandleAt(e->pos()) != -1) {
                m_hoveredShape = m_annotations[i]; break;
            }
        }
        if (prevHover != m_hoveredShape) update();
    }

    if (m_isDragging) {
        QPoint p = e->pos();
        if (m_editHandle != -1 && m_hoveredShape) {
            if (m_editHandle == 100) { m_hoveredShape->moveBy(p - m_dragOrigin); m_dragOrigin = p; }
            else { m_hoveredShape->updatePoint(m_editHandle, p); }
            update(); return;
        }
        if (m_currentTool == ScreenshotToolType::Eraser) {
            bool changed = false;
            for (int i = m_annotations.size() - 1; i >= 0; --i) {
                if (m_annotations[i]->hitTest(p)) { m_redoStack.append(m_annotations.takeAt(i)); changed = true; if(m_hoveredShape == m_redoStack.last()) m_hoveredShape = nullptr; }
            }
            if (changed) update(); return;
        }
        if (m_state == ScreenshotState::Selecting) {
            m_endPoint = e->pos();
        } else if (m_dragHandle == 8) {
            QPoint delta = e->pos() - m_dragOrigin;
            m_startPoint += delta; m_endPoint += delta;
            m_dragOrigin = e->pos();
        } else if (m_dragHandle != -1) {
            if(m_dragHandle==0) m_startPoint = p; else if(m_dragHandle==1) m_startPoint.setY(p.y()); else if(m_dragHandle==2) { m_startPoint.setY(p.y()); m_endPoint.setX(p.x()); }
            else if(m_dragHandle==3) m_endPoint.setX(p.x()); else if(m_dragHandle==4) m_endPoint = p; else if(m_dragHandle==5) m_endPoint.setY(p.y());
            else if(m_dragHandle==6) { m_endPoint.setY(p.y()); m_startPoint.setX(p.x()); } else if(m_dragHandle==7) m_startPoint.setX(p.x());
        }
        if (!selectionRect().isEmpty()) {
            if (!m_isImmediateOCR) {
                m_infoBar->updateInfo(selectionRect()); m_infoBar->show(); m_infoBar->move(selectionRect().left(), selectionRect().top() - 35); m_infoBar->raise();
            }
            updateToolbarPosition();
        }
    } else if (m_isDrawing && m_activeShape) {
        updateToolbarPosition();
        if (m_currentTool == ScreenshotToolType::Arrow || m_currentTool == ScreenshotToolType::Line || m_currentTool == ScreenshotToolType::Rect || m_currentTool == ScreenshotToolType::Ellipse || m_currentTool == ScreenshotToolType::MosaicRect) {
            if (m_activeShape->data.points.size() > 1) m_activeShape->data.points[1] = e->pos(); else m_activeShape->data.points.append(e->pos());
        } else m_activeShape->data.points.append(e->pos());
    } else updateCursor(e->pos());
    update();
}

void ScreenshotTool::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        // [用户修改要求] 右键单击触发放弃任务逻辑，且必须在 Release 时处理以完全拦截点击流，防止穿透
        if (m_isDrawing) { 
            m_isDrawing = false; 
            if (m_activeShape) {
                delete m_activeShape; 
                m_activeShape = nullptr; 
            }
            update(); 
        } else if (m_currentTool != ScreenshotToolType::None) { 
            m_currentTool = ScreenshotToolType::None; 
            m_toolbar->selectTool(ScreenshotToolType::None); 
            update(); 
        } else {
            cancel(); 
        }
        e->accept();
        return;
    }

    if (m_isDrawing) {
        m_isDrawing = false;
        if (m_activeShape) {
            m_annotations.append(m_activeShape);
            m_activeShape = nullptr;
            qDeleteAll(m_redoStack);
            m_redoStack.clear();
        }
    }
    else if (m_isDragging) {
        m_isDragging = false;
        m_editHandle = -1;
        if (m_state == ScreenshotState::Selecting) {
            if ((e->pos() - m_startPoint).manhattanLength() < 5) {
                if (!m_highlightedRect.isEmpty()) { m_startPoint = m_highlightedRect.topLeft(); m_endPoint = m_highlightedRect.bottomRight(); }
            }
            if (selectionRect().width() > 2 && selectionRect().height() > 2) {
                m_state = ScreenshotState::Editing;
                m_highlightedRect = QRect();
                m_detectedRects.clear();
            }
        }
    }
    if (m_state == ScreenshotState::Editing) {
        if (m_isImmediateOCR) {
            confirm();
            return;
        }
        updateToolbarPosition(); m_toolbar->show(); m_infoBar->updateInfo(selectionRect());
        m_infoBar->show(); m_infoBar->move(selectionRect().left(), selectionRect().top() - 35);
    }
    update();
}

void ScreenshotTool::contextMenuEvent(QContextMenuEvent* event) {
    // [用户修改要求] 彻底拦截上下文菜单事件，确保在任何情况下都不会弹出系统或第三方菜单
    event->accept();
}

void ScreenshotTool::updateToolbarPosition() {
    QRect r = selectionRect(); if(r.isEmpty()) return; m_toolbar->adjustSize();
    // 补偿 6px 的外边距，并将视觉位置贴近选区边缘（保留 4px 视觉间距）
    int x = r.right() - m_toolbar->width() + 6;
    int y = r.bottom() - 4; 
    
    if (x < -6) x = -6; 
    if (y + m_toolbar->height() - 6 > height()) y = r.top() - m_toolbar->height() + 4;
    
    m_toolbar->move(x, y); 
    if (!m_isImmediateOCR) {
        if (m_state == ScreenshotState::Editing && !m_toolbar->isVisible()) m_toolbar->show();
        if (m_toolbar->isVisible()) m_toolbar->raise();
    }
}

void ScreenshotTool::wheelEvent(QWheelEvent* event) {
    int delta = event->angleDelta().y();
    if (m_hoveredShape) {
        if (m_hoveredShape->data.type == ScreenshotToolType::Text) {
            int newSize = m_hoveredShape->data.fontSize + (delta > 0 ? 2 : -2);
            m_hoveredShape->data.fontSize = std::max(8, std::min(100, newSize));
        } else {
            int newWidth = m_hoveredShape->data.strokeWidth + (delta > 0 ? 1 : -1);
            m_hoveredShape->data.strokeWidth = std::max(1, std::min(50, newWidth));
        }
        update();
    } else {
        // 如果没有悬浮标注，修改全局设置
        if (m_currentTool == ScreenshotToolType::Text) {
            m_currentFontSize += (delta > 0 ? 2 : -2);
            m_currentFontSize = std::max(8, std::min(100, m_currentFontSize));
        } else {
            m_currentStrokeWidth += (delta > 0 ? 1 : -1);
            m_currentStrokeWidth = std::max(1, std::min(50, m_currentStrokeWidth));
        }
    }
    event->accept();
}

void ScreenshotTool::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0,0,m_screenPixmap);
    QRect r = selectionRect(); QPainterPath path; path.addRect(rect());
    if(r.isValid()) path.addRect(r); p.fillPath(path, QColor(0,0,0,120));

    if (m_state == ScreenshotState::Selecting && !m_isDragging && !m_highlightedRect.isEmpty()) {
        p.setPen(QPen(QColor(0, 120, 255, 200), 2)); p.setBrush(QColor(0, 120, 255, 30)); p.drawRect(m_highlightedRect);
    }
    if(r.isValid()) {
        p.setPen(QPen(QColor(0, 120, 255), 2)); p.drawRect(r);
        auto h = getHandleRects(); p.setBrush(Qt::white); for(auto& hr : h) p.drawEllipse(hr);
        
        // [STABILITY] 绘制阶段判空保护：确保不会在极其罕见的异步 delete 时刻访问非法地址
        p.save(); // [CRITICAL] 保存状态以防 clip 影响后续绘制
        p.setClipRect(r);
        for(auto* a : std::as_const(m_annotations)) {
            a->draw(p, m_mosaicPixmap);
            if (a == m_hoveredShape) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(Qt::cyan, 1.2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                
                // 针对不同形状绘制更有意义的连接虚线
                if (a->data.type == ScreenshotToolType::Line || a->data.type == ScreenshotToolType::Arrow) {
                    if (a->data.points.size() >= 2) p.drawLine(a->data.points[0], a->data.points[1]);
                } else if (a->data.type == ScreenshotToolType::Rect || a->data.type == ScreenshotToolType::Ellipse || a->data.type == ScreenshotToolType::MosaicRect) {
                    if (a->data.points.size() >= 2) p.drawRect(QRectF(a->data.points[0], a->data.points[1]).normalized());
                } else if (a->data.type == ScreenshotToolType::Text) {
                    // 文字工具显示边界框
                    QFont font(a->data.fontFamily, a->data.fontSize);
                    font.setBold(a->data.isBold); font.setItalic(a->data.isItalic);
                    QRect r = QFontMetrics(font).boundingRect(a->data.text);
                    r.moveTo(a->data.points[0].toPoint().x(), a->data.points[0].toPoint().y() - r.height());
                    p.drawRect(r.adjusted(-4, -2, 4, 2));
                }

                // 绘制编辑句柄
                auto handles = a->getHandles();
                for(const auto& hh : std::as_const(handles)) {
                    p.setPen(QPen(Qt::white, 1));
                    p.setBrush(QColor(0, 120, 255));
                    p.drawRect(hh);
                }
                p.restore();
            }
        }
        if(m_isDrawing && m_activeShape) {
            m_activeShape->draw(p, m_mosaicPixmap);
        }
        p.restore(); // [CRITICAL] 恢复状态，解除选区 clip
    }

    // [CRITICAL] 放大镜绘制必须在选区 clip 之外，以防被裁剪
    if (m_state == ScreenshotState::Selecting || m_isDragging || m_isDrawing || m_currentTool == ScreenshotToolType::Picker) {
        drawMagnifier(p, m_lastMouseMovePos);
    }
}

void ScreenshotTool::setTool(ScreenshotToolType t) { 
    if(m_textInput->isVisible()) commitTextInput(); 
    
    // [OPTIMIZATION] 延迟初始化马赛克底图
    if ((t == ScreenshotToolType::Mosaic || t == ScreenshotToolType::MosaicRect) && m_mosaicPixmap.isNull()) {
        // [FIX] 2026-03-xx 按照用户要求：重命名 small 为 scaledImg 以规避 MSVC/Windows.h 冲突 (#define small char)
        QImage scaledImg = m_screenImage.scaled(m_screenImage.width()/40, m_screenImage.height()/40, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        m_mosaicPixmap = QPixmap::fromImage(scaledImg.scaled(m_screenImage.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
    }

    m_currentTool = t; 
    QSettings("RapidNotes", "Screenshot").setValue("tool", static_cast<int>(t)); 
}
void ScreenshotTool::setDrawColor(const QColor& c) { 
    m_currentColor = c; 
    QSettings("RapidNotes", "Screenshot").setValue("color", c); 
    if (m_toolbar) m_toolbar->addRecentColor(c);
}
void ScreenshotTool::setDrawWidth(int w) { m_currentStrokeWidth = w; QSettings("RapidNotes", "Screenshot").setValue("strokeWidth", w); }
void ScreenshotTool::setArrowStyle(ArrowStyle s) { m_currentArrowStyle = s; QSettings("RapidNotes", "Screenshot").setValue("arrowStyle", static_cast<int>(s)); }
void ScreenshotTool::setFillEnabled(bool enabled) { m_fillEnabled = enabled; }
void ScreenshotTool::setBold(bool bold) { m_currentBold = bold; QSettings("RapidNotes", "Screenshot").setValue("bold", bold); }
void ScreenshotTool::setItalic(bool italic) { m_currentItalic = italic; QSettings("RapidNotes", "Screenshot").setValue("italic", italic); }
void ScreenshotTool::setFontFamily(const QString& family) { m_currentFontFamily = family; QSettings("RapidNotes", "Screenshot").setValue("fontFamily", family); }
void ScreenshotTool::setFontSize(int size) { m_currentFontSize = size; QSettings("RapidNotes", "Screenshot").setValue("fontSize", size); }

void ScreenshotTool::undo() { if(!m_annotations.isEmpty()) { m_redoStack.append(m_annotations.takeLast()); update(); } }
void ScreenshotTool::redo() { if(!m_redoStack.isEmpty()) { m_annotations.append(m_redoStack.takeLast()); update(); } }
void ScreenshotTool::copyToClipboard() { 
    QImage img = generateFinalImage();
    emit screenshotCaptured(img, false);
    autoSaveImage(img);
    // [OPTIMIZATION] 强制释放截图缓冲区
    m_screenPixmap = QPixmap();
    m_screenImage = QImage();
    m_mosaicPixmap = QPixmap();
    cancel(); 
}
void ScreenshotTool::save() { 
    QImage img = generateFinalImage();
    emit screenshotCaptured(img, false);
    QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString f = QFileDialog::getSaveFileName(this, "保存截图", fileName, "PNG(*.png)"); 
    if(!f.isEmpty()) img.save(f); 
    autoSaveImage(img);
    cancel(); 
}
void ScreenshotTool::confirm() { 
    if (m_isConfirmed) return;
    m_isConfirmed = true;
    QImage img = generateFinalImage();
    emit screenshotCaptured(img, m_isImmediateOCR); 
    autoSaveImage(img);
    // [OPTIMIZATION] 确认后立即销毁内存中驻留的全屏大对象
    m_screenPixmap = QPixmap();
    m_screenImage = QImage();
    m_mosaicPixmap = QPixmap();
    cancel(); 
}
void ScreenshotTool::pin() { QImage img = generateFinalImage(); if (img.isNull()) return; auto* widget = new PinnedScreenshotWidget(QPixmap::fromImage(img), selectionRect()); widget->show(); cancel(); }

QRect ScreenshotTool::selectionRect() const { return QRect(m_startPoint, m_endPoint).normalized(); }
QList<QRect> ScreenshotTool::getHandleRects() const {
    QRect r = selectionRect(); QList<QRect> l; if(r.isEmpty()) return l; int s = 10;
    l << QRect(r.left()-s/2, r.top()-s/2, s, s) << QRect(r.center().x()-s/2, r.top()-s/2, s, s)
      << QRect(r.right()-s/2, r.top()-s/2, s, s) << QRect(r.right()-s/2, r.center().y()-s/2, s, s)
      << QRect(r.right()-s/2, r.bottom()-s/2, s, s) << QRect(r.center().x()-s/2, r.bottom()-s/2, s, s)
      << QRect(r.left()-s/2, r.bottom()-s/2, s, s) << QRect(r.left()-s/2, r.center().y()-s/2, s, s);
    return l;
}
int ScreenshotTool::getHandleAt(const QPoint& p) const { auto l = getHandleRects(); for(int i=0; i<l.size(); ++i) if(l[i].contains(p)) return i; return -1; }
void ScreenshotTool::updateCursor(const QPoint& p) {
    if (m_state == ScreenshotState::Editing) {
        int handle = getHandleAt(p); if (handle != -1) {
            switch (handle) { case 0: case 4: setCursor(Qt::SizeFDiagCursor); break; case 1: case 5: setCursor(Qt::SizeVerCursor); break; case 2: case 6: setCursor(Qt::SizeBDiagCursor); break; case 3: case 7: setCursor(Qt::SizeHorCursor); break; }
            return;
        }
        if (selectionRect().contains(p)) { if (m_currentTool != ScreenshotToolType::None) setCursor(Qt::CrossCursor); else setCursor(Qt::SizeAllCursor); return; }
    }
    setCursor(Qt::ArrowCursor);
}
void ScreenshotTool::showTextInput(const QPoint& p) { 
    m_textInput->move(p); 
    m_textInput->resize(200, 30); 
    QFont font(m_currentFontFamily, m_currentFontSize);
    font.setBold(m_currentBold);
    font.setItalic(m_currentItalic);
    m_textInput->setFont(font);
    m_textInput->setStyleSheet(QString("color: %1; background: transparent;").arg(m_currentColor.name()));
    m_textInput->show(); 
    m_textInput->setFocus(); 
}
void ScreenshotTool::commitTextInput() {
    if(m_textInput->text().isEmpty()) { m_textInput->hide(); return; }
    DrawingAnnotation ann = {ScreenshotToolType::Text, {m_textInput->pos()}, m_currentColor, m_textInput->text(), m_currentStrokeWidth, LineStyle::Solid, ArrowStyle::SolidSingle, false,
                             m_currentFontFamily, m_currentFontSize, m_currentBold, m_currentItalic};
    BaseShape* shape = createShape(ann);
    if (shape) {
        m_annotations.append(shape);
        qDeleteAll(m_redoStack);
        m_redoStack.clear();
    }
    m_textInput->hide(); m_textInput->clear(); update();
}

#ifdef Q_OS_WIN
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    QList<QRect>* rects = reinterpret_cast<QList<QRect>*>(lParam);
    // 子窗口使用 GetWindowRect 即可，DWM 扩展边框通常只针对顶层窗口
    if (IsWindowVisible(hwnd)) {
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            QRect qr(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
            if (qr.width() > 5 && qr.height() > 5) rects->append(qr);
        }
    }
    return TRUE;
}
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    QList<QRect>* rects = reinterpret_cast<QList<QRect>*>(lParam);
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        TCHAR className[256]; GetClassName(hwnd, className, 256);
        if (_tcscmp(className, _T("Qt662QWindowIcon")) == 0) return TRUE;
        int cloaked = 0; DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        QRect qr = getActualWindowRect(hwnd); if (qr.width() > 10 && qr.height() > 10) { rects->append(qr); EnumChildWindows(hwnd, EnumChildProc, lParam); }
    }
    return TRUE;
}
#endif
void ScreenshotTool::detectWindows() { 
    if (m_state != ScreenshotState::Selecting) return;
    m_detectedRects.clear();
#ifdef Q_OS_WIN
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&m_detectedRects));
#endif

    // 关键优化：遍历本应用内所有活跃的 Qt 部件，实现对搜索框、分类栏等细微控件的精确侦测
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top->isVisible() && !top->isMinimized() && top != this && top->windowOpacity() > 0) {
            collectQtWidgets(top);
            
            // 2026-03-xx 引入 UI Automation 探测外部应用精细 UI 元素
#ifdef Q_OS_WIN
            collectUIAElements((HWND)top->winId());
#endif
        }
    }

    // 针对鼠标当前指向的外部窗口进行深度的 UIA 探测
#ifdef Q_OS_WIN
    POINT pt;
    if (GetCursorPos(&pt)) {
        HWND targetHwnd = WindowFromPoint(pt); 
        if (targetHwnd) {
            // 向上寻找顶层窗口
            HWND rootHwnd = GetAncestor(targetHwnd, GA_ROOT);
            if (rootHwnd) collectUIAElements(rootHwnd);
        }
    }
#endif

    // 统一坐标系转换
    QPoint offset = mapToGlobal(QPoint(0,0)); 
    for(QRect& r : m_detectedRects) r.translate(-offset);
    
    // 去重并过滤极小区域
    QList<QRect> filtered;
    for (const QRect& r : std::as_const(m_detectedRects)) {
        if (r.width() > 5 && r.height() > 5 && !filtered.contains(r)) {
            filtered.append(r);
        }
    }
    m_detectedRects = filtered;
}

void ScreenshotTool::drawMagnifier(QPainter& p, const QPoint& pos) {
    if (pos.x() < 0 || pos.y() < 0 || pos.x() >= m_screenImage.width() || pos.y() >= m_screenImage.height()) return;

    p.save();
    const int zoom = 12;
    const int cols = 21;
    const int rows = 11;
    const int magWidth = cols * zoom;
    const int magHeight = rows * zoom;
    const int infoHeight = 130;
    
    int margin = 30;
    QRect sel = selectionRect();
    QRect magRect(pos.x() + margin, pos.y() + margin, magWidth, magHeight);

    // [CRITICAL] 放大镜绘制逻辑：当鼠标在选区内时，尝试在四个方向寻找空间放置放大镜，避免遮挡。
    if (m_state == ScreenshotState::Editing && sel.contains(pos)) {
        if (sel.right() + margin + magWidth < width()) magRect.moveTo(sel.right() + margin, pos.y() - magHeight / 2);
        else if (sel.left() - margin - magWidth > 0) magRect.moveTo(sel.left() - margin - magWidth, pos.y() - magHeight / 2);
        else if (sel.top() - margin - magHeight - infoHeight > 0) magRect.moveTo(pos.x() - magWidth / 2, sel.top() - margin - magHeight - infoHeight);
        else if (sel.bottom() + margin < height()) magRect.moveTo(pos.x() - magWidth / 2, sel.bottom() + margin);
    }

    // 边界安全检查，确保放大镜和信息面板不超出屏幕
    if (magRect.right() > width()) magRect.moveRight(width() - 5);
    if (magRect.left() < 0) magRect.moveLeft(5);
    if (magRect.bottom() + infoHeight > height()) magRect.moveBottom(height() - infoHeight - 5);
    if (magRect.top() < 0) magRect.moveTop(5);
    
    p.setRenderHint(QPainter::Antialiasing, false);
    
    // 1. 绘制放大区域背景及边框
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::black);
    p.drawRect(magRect.adjusted(-1, -1, 1, 1));
    
    // 这里的源矩形取自原始屏幕像素
    QRect srcRect(pos.x() - cols/2, pos.y() - rows/2, cols, rows);
    p.drawPixmap(magRect, m_screenPixmap, srcRect);
    
    // 2. 绘制像素网格
    p.setPen(QPen(QColor(255, 255, 255, 50), 1));
    for (int i = 0; i <= magWidth; i += zoom) p.drawLine(magRect.left() + i, magRect.top(), magRect.left() + i, magRect.bottom());
    for (int i = 0; i <= magHeight; i += zoom) p.drawLine(magRect.left(), magRect.top() + i, magRect.right(), magRect.top() + i);
    
    // 3. 绘制中心像素高亮框
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(magRect.left() + (cols/2)*zoom, magRect.top() + (rows/2)*zoom, zoom, zoom);
    
    // 4. 绘制下方黑色信息面板
    QRect infoRect(magRect.left(), magRect.bottom() + 1, magWidth, infoHeight);
    p.fillRect(infoRect, Qt::black);
    
    QColor color = m_screenImage.pixelColor(pos);
    QString colorStr;
    if (m_colorFormatIndex == 0) colorStr = color.name(QColor::HexRgb).toUpper();
    else if (m_colorFormatIndex == 1) colorStr = QString("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    else {
        // HSL 格式处理
        int h = color.hslHue() < 0 ? 0 : color.hslHue();
        int s = int(color.hslSaturationF() * 100);
        int l = int(color.lightnessF() * 100);
        colorStr = QString("HSL(%1, %2, %3)").arg(h).arg(s).arg(l);
    }

    p.setPen(Qt::white);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font("Microsoft YaHei", 10);
    p.setFont(font);

    // 第一行：坐标 (x , y)
    QString coordStr = QString("(%1 , %2)").arg(pos.x()).arg(pos.y());
    p.drawText(infoRect.adjusted(0, 10, 0, 0), Qt::AlignHCenter | Qt::AlignTop, coordStr);

    // 第二行：颜色预览方块 + 色值文本
    int textWidth = p.fontMetrics().horizontalAdvance(colorStr);
    int totalContentWidth = 16 + 8 + textWidth; // 方块(16) + 间距(8) + 文本
    int startX = infoRect.left() + (magWidth - totalContentWidth) / 2;
    int line2Y = infoRect.top() + 38;
    
    p.setBrush(color);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(startX, line2Y, 16, 16);
    
    p.setPen(Qt::white);
    p.drawText(startX + 24, line2Y + 13, colorStr);

    // 第三、四、五行：功能提示
    font.setPointSize(9);
    p.setFont(font);
    p.drawText(infoRect.adjusted(0, 65, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "Shift:  切换颜色格式");
    p.drawText(infoRect.adjusted(0, 86, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "C:  复制色值");
    p.drawText(infoRect.adjusted(0, 107, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "M:  复制坐标");

    p.restore();
}

void ScreenshotTool::collectQtWidgets(QWidget* parent) {
    if (!parent || !parent->isVisible()) return;

    // 记录当前部件的全局几何信息
    QRect r = parent->rect();
    QPoint globalPos = parent->mapToGlobal(QPoint(0,0));
    r.moveTo(globalPos);
    m_detectedRects.append(r);

    // [CRITICAL] 特殊处理：针对 QAbstractItemView (TreeView/ListView) 扫描内部 Item 矩形
    // 这解决了用户提到的“侧边栏分类检测不到”的问题
    if (auto* view = qobject_cast<QAbstractItemView*>(parent)) {
        detectItemViewRects(view);
    }

    // 递归处理子部件
    const QObjectList& children = parent->children();
    for (QObject* childObj : children) {
        QWidget* child = qobject_cast<QWidget*>(childObj);
        if (child && child->isVisible() && !child->rect().isEmpty()) {
            collectQtWidgets(child);
        }
    }
}

void ScreenshotTool::detectItemViewRects(QAbstractItemView* view) {
    if (!view || !view->model()) return;

    // 仅遍历可见区域内的索引，保证性能
    QRect viewportRect = view->viewport()->rect();
    QModelIndex topIndex = view->indexAt(viewportRect.topLeft());
    if (!topIndex.isValid()) return;

    // 简单高效的遍历：从顶层可见项向下扫描
    // 我们限制扫描数量以防万一模型巨大导致卡顿
    int count = 0;
    QModelIndex idx = topIndex;
    
    auto* treeView = qobject_cast<QTreeView*>(view);
    auto* listView = qobject_cast<QListView*>(view);

    while (idx.isValid() && count < 200) {
        QRect r = view->visualRect(idx);
        if (!r.isEmpty()) {
            QPoint globalPos = view->viewport()->mapToGlobal(r.topLeft());
            r.moveTo(globalPos);
            m_detectedRects.append(r);
        }
        
        // 尝试寻找下一个可见索引
        QModelIndex nextIdx;
        if (treeView) {
            nextIdx = treeView->indexBelow(idx);
        } else {
            // 对于 ListView 或 TableView，移动到下一行
            nextIdx = view->model()->index(idx.row() + 1, idx.column(), idx.parent());
        }
        
        idx = nextIdx;
        count++;
        
        // 如果超出了可视区域，可以停止 (可选优化)
        if (!r.isEmpty() && r.top() > viewportRect.bottom()) break;
    }
}

#ifdef Q_OS_WIN
void ScreenshotTool::collectUIAElements(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    // [STABILITY] COM 初始化保护：确保 UI Automation API 能在任何线程环境下正常工作
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // 2026-03-xx 按照用户要求：使用 Windows UI Automation API 探测外部应用精细 UI 元素
    IUIAutomation* pAutomation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&pAutomation));
    if (FAILED(hr) || !pAutomation) return;

    IUIAutomationElement* pRoot = nullptr;
    hr = pAutomation->ElementFromHandle(hwnd, &pRoot);
    if (SUCCEEDED(hr) && pRoot) {
        IUIAutomationCondition* pCondition = nullptr;
        pAutomation->CreateTrueCondition(&pCondition); // 采集所有元素
        
        IUIAutomationElementArray* pFound = nullptr;
        // 限制探测深度和数量以保持性能。TreeScope_Descendants 会遍历整个子树
        hr = pRoot->FindAll(TreeScope_Children, pCondition, &pFound); 
        
        if (SUCCEEDED(hr) && pFound) {
            int length = 0;
            pFound->get_Length(&length);
            for (int i = 0; i < length && i < 100; ++i) {
                IUIAutomationElement* pElement = nullptr;
                pFound->GetElement(i, &pElement);
                if (pElement) {
                    RECT rect;
                    if (SUCCEEDED(pElement->get_CurrentBoundingRectangle(&rect))) {
                        QRect qr(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
                        if (qr.width() > 10 && qr.height() > 10) m_detectedRects.append(qr);
                    }
                    pElement->Release();
                }
            }
            pFound->Release();
        }
        if (pCondition) pCondition->Release();
        pRoot->Release();
    }
    pAutomation->Release();
    
    // 对应释放 COM 环境
    CoUninitialize();
}
#endif
void ScreenshotTool::executeOCR() {
    QImage img = generateFinalImage();
    emit screenshotCaptured(img, true);
    cancel();
}

QImage ScreenshotTool::generateFinalImage() {
    QRect r = selectionRect(); if(r.isEmpty()) return QImage();
    QPixmap p = m_screenPixmap.copy(r); QPainter painter(&p); painter.translate(-r.topLeft());
    for(auto* a : std::as_const(m_annotations)) a->draw(painter, m_mosaicPixmap);
    return p.toImage();
}

void ScreenshotTool::autoSaveImage(const QImage& img) {
    if (img.isNull()) return;
    
    QSettings settings("RapidNotes", "Screenshot");
    QString defaultPath = QCoreApplication::applicationDirPath() + "/RPN_screenshot";
    QString savePath = settings.value("savePath", defaultPath).toString();
    
    QDir dir(savePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QString fileName = QString("RPN_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fullPath = dir.absoluteFilePath(fileName);
    
    if (img.save(fullPath)) {
        // 使用非阻塞彩色反馈告知用户已自动保存 (2026-03-xx 统一修改为 700ms)
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("[OK] 已自动保存至:\n%1").arg(fullPath), 700);
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "[ERR] 自动保存失败，请检查路径权限", 700);
    }
}
void ScreenshotTool::keyPressEvent(QKeyEvent* e) { 
    if(e->key() == Qt::Key_Escape) {
        // [MODIFIED] 截图工具是瞬时工具，如果正在编辑文字则退出文字，否则关闭截图
        if (m_textInput->isVisible()) {
            commitTextInput();
            e->accept();
            return;
        }
        cancel();
        return;
    }
    else if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) { if(m_state == ScreenshotState::Editing) confirm(); }
    else if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_Z) undo();
    else if (e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && e->key() == Qt::Key_Z) redo();
    else if (e->modifiers() == Qt::ControlModifier && e->key() == Qt::Key_O) executeOCR();
    else if (e->key() == Qt::Key_F) pin();
    else if (e->key() == Qt::Key_C) { 
        // 如果放大镜可见，C 键用于复制当前像素色值
        if (m_state == ScreenshotState::Selecting || m_isDragging || m_isDrawing || m_currentTool == ScreenshotToolType::Picker) {
            QColor color = m_screenImage.pixelColor(m_lastMouseMovePos);
            QString colorStr;
            if (m_colorFormatIndex == 0) colorStr = color.name(QColor::HexRgb).toUpper();
            else if (m_colorFormatIndex == 1) colorStr = QString("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
            else colorStr = QString("HSL(%1, %2, %3)").arg(color.hslHue() < 0 ? 0 : color.hslHue()).arg(int(color.hslSaturationF()*100)).arg(int(color.lightnessF()*100));
            
            ClipboardMonitor::instance().forceNext();
            QApplication::clipboard()->setText(colorStr);
            // 2026-03-13 按照用户要求：提示时长缩短为 700ms
            ToolTipOverlay::instance()->showText(QCursor::pos(), QString("已复制色值: %1").arg(colorStr), 700);
        } else {
            m_toolbar->selectTool(ScreenshotToolType::Picker); 
        }
    }
    else if (e->key() == Qt::Key_Shift) {
        m_colorFormatIndex = (m_colorFormatIndex + 1) % 3;
        update();
    }
    else if (e->key() == Qt::Key_M) {
        QString coordStr = QString("%1, %2").arg(m_lastMouseMovePos.x()).arg(m_lastMouseMovePos.y());
        ClipboardMonitor::instance().forceNext();
        QApplication::clipboard()->setText(coordStr);
        // 2026-03-13 按照用户要求：提示时长缩短为 700ms
        ToolTipOverlay::instance()->showText(QCursor::pos(), QString("已复制坐标: %1").arg(coordStr), 700);
    }
}
void ScreenshotTool::mouseDoubleClickEvent(QMouseEvent* e) { if(selectionRect().contains(e->pos())) confirm(); }

bool ScreenshotTool::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_textInput && event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            // 文字输入时 Esc 仅退出输入模式，不关闭截图工具。事件在此处被消费，不再传递。
            commitTextInput();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}