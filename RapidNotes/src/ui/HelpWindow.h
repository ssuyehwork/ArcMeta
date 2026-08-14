#ifndef HELPWINDOW_H
#define HELPWINDOW_H

#include "FramelessDialog.h"
#include <QListWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QHBoxLayout>

class HelpWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit HelpWindow(QWidget* parent = nullptr);

private:
    struct HelpItem {
        QString key;
        QString description;
    };

    void initUI();
    void setupData();
    void onItemSelected(QListWidgetItem* item);

    QListWidget* m_listWidget = nullptr;
    QTextBrowser* m_detailView = nullptr;
    QList<HelpItem> m_helpData;
};

#endif // HELPWINDOW_H
