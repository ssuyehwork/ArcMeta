#ifndef SECURITYLOCKDIALOG_H
#define SECURITYLOCKDIALOG_H

#include "FramelessDialog.h"

/**
 * @brief 安全锁定对话框，支持通过超级快捷键触发抢救模式
 */
class SecurityLockDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit SecurityLockDialog(const QString& message, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLabel* m_lblMessage;
};

#endif // SECURITYLOCKDIALOG_H
