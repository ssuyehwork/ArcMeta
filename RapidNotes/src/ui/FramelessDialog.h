#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>

/**
 * @brief 无边框对话框基类，自带标题栏、关闭按钮、阴影、置顶
 */
class FramelessDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramelessDialog(const QString& title, QWidget* parent = nullptr);
    virtual ~FramelessDialog() = default;

    void setStayOnTop(bool stay);
    QWidget* getContentArea() const { return m_contentArea; }
    virtual void updateShortcuts();

private slots:
    void toggleStayOnTop(bool checked);
    void toggleMaximize();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

    QWidget* m_contentArea;
    QVBoxLayout* m_mainLayout;
    QVBoxLayout* m_outerLayout;
    QWidget* m_container;
    class QGraphicsDropShadowEffect* m_shadow;
    QLabel* m_titleLabel;
    QPushButton* m_btnPin;
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;

    virtual void loadWindowSettings();
    virtual void saveWindowSettings();

private:
    enum ResizeEdge {
        None = 0,
        Top = 0x1,
        Bottom = 0x2,
        Left = 0x4,
        Right = 0x8,
        TopLeft = Top | Left,
        TopRight = Top | Right,
        BottomLeft = Bottom | Left,
        BottomRight = Bottom | Right
    };

    ResizeEdge getEdge(const QPoint& pos);
    void updateCursor(ResizeEdge edge);

    QPoint m_dragPos;
    bool m_isStayOnTop = false; 
    bool m_firstShow = true;
    bool m_isResizing = false;
    ResizeEdge m_resizeEdge = None;
};

/**
 * @brief 无边框文本输入对话框
 */
class FramelessInputDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessInputDialog(const QString& title, const QString& label, 
                                  const QString& initial = "", QWidget* parent = nullptr);
    QString text() const { return m_edit->text().trimmed(); }
    void setEchoMode(QLineEdit::EchoMode mode) { m_edit->setEchoMode(mode); }

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* m_edit;
};

/**
 * @brief 无边框确认提示框
 */
class FramelessMessageBox : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessMessageBox(const QString& title, const QString& text, QWidget* parent = nullptr);

signals:
    void confirmed();
    void cancelled();

protected:
    void showEvent(QShowEvent* event) override; // 2026-03-22 [NEW] 用于锁定初始焦点

private:
    QPushButton* m_btnOk; // 2026-03-22 [NEW] 记录确定按钮引用
};

/**
 * @brief 无边框进度对话框
 */
class FramelessProgressDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessProgressDialog(const QString& title, const QString& label, 
                                    int min = 0, int max = 100, QWidget* parent = nullptr);

    void setValue(int value);
    void setLabelText(const QString& text);
    void setRange(int min, int max);
    bool wasCanceled() const { return m_wasCanceled; }

signals:
    void canceled();

private:
    class QProgressBar* m_progress;
    QLabel* m_statusLabel;
    bool m_wasCanceled = false;
};

#endif // FRAMELESSDIALOG_H
