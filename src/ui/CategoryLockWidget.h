#ifndef CATEGORYLOCKWIDGET_H
#define CATEGORYLOCKWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>

namespace QuarkMeta {

class CategoryLockWidget : public QWidget {
    Q_OBJECT
public:
    explicit CategoryLockWidget(QWidget* parent = nullptr);
    
    void setCategory(int id, const QString& hint);
    void focusInput();
    void clearInput();

signals:
    void unlocked(int id);
    void escPressed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onVerify();

private:
    int m_catId = -1;
    QLabel* m_hintLabel;
    QLineEdit* m_pwdEdit;
};

} // namespace QuarkMeta

#endif // CATEGORYLOCKWIDGET_H
