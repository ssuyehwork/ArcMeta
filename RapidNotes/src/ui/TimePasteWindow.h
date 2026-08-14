#ifndef TIMEPASTEWINDOW_H
#define TIMEPASTEWINDOW_H

#include "FramelessDialog.h"
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTimer>

class TimePasteWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit TimePasteWindow(QWidget* parent = nullptr);
    ~TimePasteWindow();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void updateDateTime();
    void onDigitPressed(int digit);

private:
    void initUI();
    QString getRadioStyle();

    QLabel* m_dateLabel;
    QLabel* m_timeLabel;
    QRadioButton* m_radioPrev;
    QRadioButton* m_radioNext;
    QButtonGroup* m_buttonGroup;
    QTimer* m_timer;
    QPoint m_dragPos;
};

#endif // TIMEPASTEWINDOW_H
