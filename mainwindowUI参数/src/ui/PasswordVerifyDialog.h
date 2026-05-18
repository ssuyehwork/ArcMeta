#ifndef PASSWORDVERIFYDIALOG_H
#define PASSWORDVERIFYDIALOG_H

#include "FramelessDialog.h"
#include <QLineEdit>

/**
 * @brief 导出场景专用的密码验证对话框
 * 2026-03-20 按照用户要求，增加导出前的身份验证屏障
 */
class PasswordVerifyDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit PasswordVerifyDialog(const QString& title, const QString& message, QWidget* parent = nullptr);

    static bool verify();
    QString password() const { return m_pwdEdit->text(); }

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit* m_pwdEdit;
};

#endif // PASSWORDVERIFYDIALOG_H
