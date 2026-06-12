#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QPoint>
#include <QMouseEvent>
#include <QKeyEvent>

namespace ArcMeta {

/**
 * @brief 无边框对话框基类，自带标题栏、关闭按钮（扁平化设计）
 * 适配 ArcMeta 风格，参考旧版 RapidNotes 基因实现
 */
class FramelessDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramelessDialog(const QString& title, QWidget* parent = nullptr);
    virtual ~FramelessDialog() = default;

    QWidget* getContentArea() const { return m_contentArea; }

protected:
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    QWidget* m_contentArea;
    QVBoxLayout* m_mainLayout;
    QVBoxLayout* m_outerLayout;
    QWidget* m_container;
    QLabel* m_titleLabel;
    QPushButton* m_pinBtn;
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;

private:
    QPoint m_dragPos;
    bool m_isDragging = false;
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

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit* m_edit;
};

/**
 * @brief 无边框确认/消息对话框
 */
class FramelessConfirmDialog : public FramelessDialog {
    Q_OBJECT
public:
    enum Type { Info, Warning, Question };
    explicit FramelessConfirmDialog(const QString& title, const QString& message,
                                    Type type = Info, QWidget* parent = nullptr);

    // 2026-07-xx 静态快捷方法
    static bool showConfirm(const QString& title, const QString& message, Type type = Info, QWidget* parent = nullptr);
};

/**
 * @brief 无边框颜色选择对话框
 */
class FramelessColorDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessColorDialog(const QColor& initial, QWidget* parent = nullptr);
    QColor selectedColor() const;

    static QColor getColor(const QColor& initial, QWidget* parent = nullptr);

private:
    class ColorPicker* m_picker;
};

/**
 * @brief 无边框目录选择对话框
 */
class FramelessFolderDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit FramelessFolderDialog(const QString& title, QWidget* parent = nullptr);
    QString selectedPath() const;

    static QString getExistingDirectory(const QString& title, QWidget* parent = nullptr);

private:
    class QTreeView* m_tree;
    class QFileSystemModel* m_model;
};

} // namespace ArcMeta

#endif // FRAMELESSDIALOG_H
