#ifndef PASSWORDGENERATORWINDOW_H
#define PASSWORDGENERATORWINDOW_H

#include "FramelessDialog.h"
#include <QLineEdit>
#include <QProgressBar>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class PasswordGeneratorWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit PasswordGeneratorWindow(QWidget* parent = nullptr);
    ~PasswordGeneratorWindow();

private slots:
    void generatePassword();

private:
    void initUI();
    QWidget* createDisplayArea();
    QWidget* createControlsArea();
    QString generateSecurePassword(int length, bool upper, bool lower, bool digits, bool symbols, bool excludeAmbiguous);

    QLineEdit* m_usageEntry;
    QLineEdit* m_passEntry;
    QProgressBar* m_strengthBar;
    QLabel* m_lengthLabel;
    QSlider* m_lengthSlider;
    QCheckBox* m_checkUpper;
    QCheckBox* m_checkLower;
    QCheckBox* m_checkDigits;
    QCheckBox* m_checkSymbols;
    QCheckBox* m_excludeAmbiguous;
    QLabel* m_statusLabel;
    QPoint m_dragPos;
};

#endif // PASSWORDGENERATORWINDOW_H
