#ifndef ACTIVATIONDIALOG_H
#define ACTIVATIONDIALOG_H

#include "FramelessDialog.h"

/**
 * @brief 强制激活引导对话框
 */
class ActivationDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit ActivationDialog(const QString& reason, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onVerifyClicked();

private:
    QLineEdit* m_editKey;
    QLabel* m_lblReason;
};

#endif // ACTIVATIONDIALOG_H
