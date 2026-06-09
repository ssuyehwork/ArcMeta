#ifndef KEYWORDSEARCHWINDOW_H
#define KEYWORDSEARCHWINDOW_H

#include "FramelessDialog.h"

class KeywordSearchWidget;

class KeywordSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit KeywordSearchWindow(QWidget* parent = nullptr);
protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    KeywordSearchWidget* m_searchWidget;
};

#endif // KEYWORDSEARCHWINDOW_H
