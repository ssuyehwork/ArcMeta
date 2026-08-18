#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>

namespace QuarkMeta {

/**
 * @brief 可撤销操作反馈 Toast 浮窗 (Snackbar 风格)
 */
class UndoToastOverlay : public QWidget {
    Q_OBJECT
public:
    static UndoToastOverlay* instance();

    /**
     * @brief 弹出操作成功与撤销提示
     * @param parent 挂载的父窗口 (通常为 MainWindow)
     * @param message 提示文案（如 "成功重命名 26 个项目"）
     * @param undoCallback 点击“撤销”时的回调处理函数
     * @param durationMs 显示持续时间（默认 5000ms）
     */
    void showToast(QWidget* parent, 
                   const QString& message, 
                   std::function<void()> undoCallback, 
                   int durationMs = 5000);

    void hideToast();

protected:
    explicit UndoToastOverlay(QWidget* parent = nullptr);
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_iconLabel = nullptr;
    QLabel* m_msgLabel = nullptr;
    QPushButton* m_btnUndo = nullptr;
    QWidget* m_separator = nullptr;
    QPushButton* m_btnClose = nullptr;

    QTimer m_autoHideTimer;
    QPropertyAnimation* m_fadeAnim = nullptr;
    std::function<void()> m_undoCallback = nullptr;
};

} // namespace QuarkMeta
