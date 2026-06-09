#ifndef FILESEARCHWINDOW_H
#define FILESEARCHWINDOW_H

#include "FramelessDialog.h"

class FileSearchWidget;

class FileSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit FileSearchWindow(QWidget* parent = nullptr);
protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    FileSearchWidget* m_searchWidget;
};

#endif // FILESEARCHWINDOW_H
