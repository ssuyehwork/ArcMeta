#ifndef CLEANLISTVIEW_H
#define CLEANLISTVIEW_H

#include <QListView>

class CleanListView : public QListView {
    Q_OBJECT
public:
    explicit CleanListView(QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void internalMoveRequested(const QList<int>& ids, int row);
};

#endif // CLEANLISTVIEW_H
