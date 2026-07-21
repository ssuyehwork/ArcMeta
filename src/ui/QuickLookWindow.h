#pragma once

#include <QWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPushButton>
#include <QSlider>
#include <QHBoxLayout>

namespace ArcMeta {

class QuickLookGraphicsView : public QGraphicsView {
    Q_OBJECT
public:
    explicit QuickLookGraphicsView(QWidget* parent = nullptr);
    void setPixmap(const QPixmap& pixmap);
    void fitImage();
    void setZoomOriginal();
    void clear();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateCursor();

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
    double m_currentScale = 1.0;
    bool m_isFitMode = true;
};

class QuickLookWindow : public QWidget {
    Q_OBJECT
public:
    static QuickLookWindow& instance();

    void previewFile(const QString& path);
    void preview(const QString& filePath);
    void closePreview();

signals:
    void ratingRequested(int rating);
    void colorRequested(const QString& color);
    void prevRequested();
    void nextRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QuickLookWindow();
    ~QuickLookWindow() override;

    void setupUi();
    void renderImage(const QString& path);
    void renderText(const QString& path);
    
    QString detectEncoding(const QByteArray& data);
    bool isBinary(const QByteArray& data);

    QuickLookGraphicsView* m_graphicsView = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_infoLabel = nullptr;
    QWidget* m_container = nullptr;
    
    QString m_currentPath;
    bool m_ignoreDeactivate = false;
};

} // namespace ArcMeta
