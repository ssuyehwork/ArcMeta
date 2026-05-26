#ifndef CATEGORYPASSWORDDIALOG_H
#define CATEGORYPASSWORDDIALOG_H

#include "FramelessDialog.h"
#include <QLineEdit>
#include <QTextEdit>

class CategoryPasswordDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit CategoryPasswordDialog(const QString& title, QWidget* parent = nullptr);
    
    void setInitialData(const QString& hint);
    
    QString password() const;
    QString confirmPassword() const;
    QString passwordHint() const;

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* m_pwdEdit;
    QLineEdit* m_confirmEdit;
    QTextEdit* m_hintEdit;
};

#endif // CATEGORYPASSWORDDIALOG_H
