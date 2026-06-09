#ifndef TAGMANAGERWINDOW_H
#define TAGMANAGERWINDOW_H

#include "FramelessDialog.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QPoint>
#include <QKeyEvent>

class TagManagerWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit TagManagerWindow(QWidget* parent = nullptr);
    ~TagManagerWindow();

public slots:
    void refreshData();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void handleRename();
    void handleDelete();
    void handleSearch(const QString& text);
    void onTagItemChanged(QTableWidgetItem* item);

    QTableWidget* m_tagTable;
    QLineEdit* m_searchEdit;
    QPoint m_dragPos;
};

#endif // TAGMANAGERWINDOW_H
