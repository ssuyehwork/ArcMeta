#ifndef COLORPICKERWINDOW_H
#define COLORPICKERWINDOW_H

#include "FramelessDialog.h"
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QStackedWidget>
#include <QScrollArea>
#include <QTimer>
#include <QFrame>

/**
 * @brief 专业颜色管理器 Pro
 */
class ColorPickerWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit ColorPickerWindow(QWidget* parent = nullptr);
    ~ColorPickerWindow();

    void showNotification(const QString& message, bool isError = false);
    void useColor(const QString& hex);

public slots:
    void startScreenPicker();   

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // 工具按钮槽
    void openColorPicker();      
    void openPixelRuler();      
    void extractFromImage();     
    void addToFavorites();       

    // 颜色更新槽
    void applyHexColor();
    void applyRgbColor();
    void copyHexValue();
    void copyRgbValue();
    
    // 渐变生成
    void generateGradient();

    // 视图切换
    void switchView(const QString& value);

    // 右键菜单
    void showColorContextMenu(const QString& colorHex, const QPoint& globalPos);

    // 图片处理
    void processImage(const QString& filePath, const QImage& image = QImage());
    void pasteImage();

private:
    void initUI();
    void createRightPanel(QWidget* parent);
    void updateColorDisplay();
    
    // 辅助组件创建
    QWidget* createColorTile(QWidget* parent, const QString& color);
    QWidget* createFavoriteTile(QWidget* parent, const QString& color);
    
    // 数据持久化
    QStringList loadFavorites();
    void saveFavorites();
    void updateFavoritesDisplay();
    void addSpecificColorToFavorites(const QString& color);
    void removeFavorite(const QString& color);

    // 颜色计算辅助
    QString rgbToHex(int r, int g, int b);
    QColor hexToColor(const QString& hex);
    QString colorToHex(const QColor& c);
    QStringList extractDominantColors(const QImage& img, int num);

    // --- UI 组件 ---
    // 左侧
    // [CRITICAL] 核心状态变量，记录当前吸取的颜色。
    QString m_currentColor = "#D64260";
    QWidget* m_colorDisplay;
    QLabel* m_colorLabel;
    QLineEdit* m_hexEntry;
    QLineEdit* m_rEntry;
    QLineEdit* m_gEntry;
    QLineEdit* m_bEntry;
    
    QLineEdit* m_gradStart;
    QLineEdit* m_gradEnd;
    QLineEdit* m_gradSteps;
    QString m_gradMode = "变暗";

    QWidget* m_imagePreviewFrame;
    QLabel* m_imagePreviewLabel;

    // 右侧
    QStackedWidget* m_stack;
    QScrollArea* m_favScroll;
    QScrollArea* m_gradScroll;
    QScrollArea* m_extractScroll;
    
    QWidget* m_favContent;
    QWidget* m_gradContent;
    QWidget* m_extractContent;
    
    QWidget* m_dropHintContainer;
    QWidget* m_favGridContainer;
    QWidget* m_gradGridContainer;
    QWidget* m_extractGridContainer;

    // 状态
    QString m_currentImagePath = "";
    // [CRITICAL] 收藏夹列表，持久化存储用户喜爱的颜色。
    QStringList m_favorites;
    QFrame* m_notification = nullptr;

};

#endif // COLORPICKERWINDOW_H