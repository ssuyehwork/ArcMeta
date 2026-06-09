#ifndef OCRRESULTWINDOW_H
#define OCRRESULTWINDOW_H

#include "FramelessDialog.h"
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class OCRResultWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit OCRResultWindow(const QImage& image, int contextId = -1, QWidget* parent = nullptr);
    void setRecognizedText(const QString& text, int contextId);

private slots:
    void onCopyClicked();
    void onTypesettingClicked();

private:
    QPlainTextEdit* m_textEdit;
    QCheckBox* m_autoCopyCheck;
    QImage m_image;
    int m_contextId;
};

#endif // OCRRESULTWINDOW_H
