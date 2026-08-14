#ifndef CLICKABLELINEEDIT_H
#define CLICKABLELINEEDIT_H

#include <QLineEdit>
#include <QMouseEvent>

class ClickableLineEdit : public QLineEdit {
    Q_OBJECT
public:
    using QLineEdit::QLineEdit;
signals:
    void doubleClicked();
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) emit doubleClicked();
        QLineEdit::mouseDoubleClickEvent(event);
    }
};

#endif // CLICKABLELINEEDIT_H