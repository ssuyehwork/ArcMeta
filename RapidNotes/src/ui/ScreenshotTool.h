#ifndef SCREENSHOTTOOL_H
#define SCREENSHOTTOOL_H

#include <QWidget>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QLineEdit>
#include <QButtonGroup>
#include <QMenu>
#include <QColorDialog>
#include <QList>
#include <QAbstractItemView>
#include <functional>
#include <utility>

enum class ScreenshotState { Selecting, Editing };
enum class ScreenshotToolType { None, Rect, Ellipse, Arrow, Line, Pen, Marker, Text, Mosaic, MosaicRect, Eraser, Picker };
enum class ArrowStyle { 
    SolidSingle, OutlineSingle, 
    SolidDouble, OutlineDouble, 
    SolidDot, OutlineDot,
    Thin,
    Dimension
};
enum class LineStyle { Solid, Dash, Dot };

struct DrawingAnnotation {
    ScreenshotToolType type;
    QList<QPointF> points;
    QColor color;
    QString text;
    int strokeWidth;
    LineStyle lineStyle;
    ArrowStyle arrowStyle;
    bool isFilled = false;
    // 文本相关属性
    QString fontFamily = "Microsoft YaHei";
    int fontSize = 12;
    bool isBold = false;
    bool isItalic = false;
};

class BaseShape {
public:
    BaseShape(const DrawingAnnotation& ann) : data(ann) {}
    virtual ~BaseShape() = default;
    virtual void draw(QPainter& painter, const QPixmap& mosaicPixmap) const = 0;
    virtual bool hitTest(const QPoint& pos) const = 0;
    virtual QList<QRect> getHandles() const { return {}; }
    virtual int getHandleAt(const QPoint& pos) const;
    virtual void updatePoint(int index, const QPoint& pos) { if(index >= 0 && index < data.points.size()) data.points[index] = pos; }
    virtual void moveBy(const QPoint& delta);
    DrawingAnnotation data;
};

class RectShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
    void updatePoint(int index, const QPoint& pos) override;
};

class EllipseShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
    void updatePoint(int index, const QPoint& pos) override;
};

class LineShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
};

class ArrowShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
};

class PenShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
};

class MarkerShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
};

class TextShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
};

class MosaicShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
};

class MosaicRectShape : public BaseShape {
public:
    using BaseShape::BaseShape;
    void draw(QPainter& painter, const QPixmap& mosaicPixmap) const override;
    bool hitTest(const QPoint& pos) const override;
    QList<QRect> getHandles() const override;
    void updatePoint(int index, const QPoint& pos) override;
};

class ScreenshotTool;
class ScreenshotToolbar;

class PinnedScreenshotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PinnedScreenshotWidget(const QPixmap& pixmap, const QRect& screenRect, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    QPixmap m_pixmap;
    QPoint m_dragPos;
};

class ScreenshotToolbar : public QWidget {
    Q_OBJECT
public:
    explicit ScreenshotToolbar(ScreenshotTool* tool);
    void addToolButton(QBoxLayout* layout, ScreenshotToolType type, const QString& iconType, const QString& tip);
    void addActionButton(QBoxLayout* layout, const QString& iconName, const QString& tip, std::function<void()> func);
    void selectTool(ScreenshotToolType type);
    void updateArrowButtonIcon(ArrowStyle style);
    void addRecentColor(const QColor& c, bool save = true);
    void syncColorSelection(const QColor& color);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void createOptionWidget();
    void showArrowMenu();

public:
    ScreenshotTool* m_tool;
    QMap<ScreenshotToolType, QPushButton*> m_buttons;
    QWidget* m_optionWidget = nullptr;
    QPushButton* m_arrowStyleBtn = nullptr;
    QPushButton* m_outlineBtn = nullptr;
    QPushButton* m_solidBtn = nullptr;
    QPushButton* m_wheelBtn = nullptr;
    QBoxLayout* m_recentLayout = nullptr;
    QWidget* m_textOptionWidget = nullptr;
    QWidget* m_sizeOptionWidget = nullptr;
    QFrame* m_textDivider = nullptr;
    QFrame* m_horizontalDivider = nullptr;
    QPushButton* m_boldBtn = nullptr;
    QPushButton* m_italicBtn = nullptr;
    QPushButton* m_removeColorBtn = nullptr; // 用于删除最近颜色

    bool m_isDragging = false;
    QPoint m_dragPosition;

    QButtonGroup* m_colorGroup = nullptr;
    QButtonGroup* m_sizeGroup = nullptr;
    QPushButton* m_paletteBtn = nullptr;
};

class SelectionInfoBar : public QWidget {
    Q_OBJECT
    friend class ScreenshotToolbar;
public:
    explicit SelectionInfoBar(QWidget* parent = nullptr);
    void updateInfo(const QRect& rect);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_text;
};

class ScreenshotTool : public QWidget {
    Q_OBJECT
    friend class ScreenshotToolbar;
public:
    explicit ScreenshotTool(QWidget* parent = nullptr);
    ~ScreenshotTool() override;
    
    void setDrawColor(const QColor& color);
    void setDrawWidth(int width);
    void setArrowStyle(ArrowStyle style);
    void setFillEnabled(bool enabled);
    void setBold(bool bold);
    void setItalic(bool italic);
    void setFontFamily(const QString& family);
    void setFontSize(int size);
    
    void updateToolbarPosition();
    void undo();
    void redo();
    void copyToClipboard();
    void save();
    void confirm();
    void pin();
    void cancel(); 
    void executeOCR();

    void setImmediateOCRMode(bool enabled) { m_isImmediateOCR = enabled; }

signals:
    void screenshotCaptured(const QImage& image, bool isOcr = false);
    void screenshotCanceled();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void setTool(ScreenshotToolType type);

private:
    QRect selectionRect() const;
    QList<QRect> getHandleRects() const;
    int getHandleAt(const QPoint& pos) const;
    void updateCursor(const QPoint& pos);
    void showTextInput(const QPoint& pos);
    void commitTextInput();
    QImage generateFinalImage();
    void autoSaveImage(const QImage& img);
    void detectWindows();
    void collectQtWidgets(QWidget* parent);
    void detectItemViewRects(QAbstractItemView* view);
#ifdef Q_OS_WIN
    void collectUIAElements(HWND hwnd);
#endif
    void drawMagnifier(QPainter& p, const QPoint& pos);

    QPixmap m_screenPixmap;
    QImage m_screenImage;
    QPixmap m_mosaicPixmap;
    
    ScreenshotState m_state = ScreenshotState::Selecting;
    ScreenshotToolType m_currentTool = ScreenshotToolType::None;
    
    QList<QRect> m_detectedRects;
    QRect m_highlightedRect;

    QPoint m_startPoint, m_endPoint;
    QPoint m_dragOrigin; // 新增：用于记录拖拽操作的起始点，避免污染 m_startPoint
    bool m_isDragging = false;
    int m_dragHandle = -1; 
    bool m_isConfirmed = false;

    QList<BaseShape*> m_annotations;
    QList<BaseShape*> m_redoStack;
    BaseShape* m_activeShape = nullptr;
    BaseShape* m_hoveredShape = nullptr;
    int m_editHandle = -1;
    DrawingAnnotation m_currentAnnotation;
    bool m_isDrawing = false;

    ScreenshotToolbar* m_toolbar = nullptr;
    SelectionInfoBar* m_infoBar = nullptr;
    QLineEdit* m_textInput = nullptr;
    QPoint m_lastMouseMovePos;
    int m_colorFormatIndex = 0; // 0: Hex, 1: RGB, 2: HSL
    bool m_isImmediateOCR = false;

    QColor m_currentColor = QColor(255, 50, 50); 
    int m_currentStrokeWidth = 3; 
    ArrowStyle m_currentArrowStyle = ArrowStyle::SolidSingle;
    bool m_fillEnabled = false;
    LineStyle m_currentLineStyle = LineStyle::Solid;

    QString m_currentFontFamily = "Microsoft YaHei";
    int m_currentFontSize = 14;
    bool m_currentBold = true;
    bool m_currentItalic = false;
};

#endif // SCREENSHOTTOOL_H
