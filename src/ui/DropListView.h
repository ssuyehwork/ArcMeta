#ifndef DROPLISTVIEW_H
#define DROPLISTVIEW_H

#include <QListView>

namespace ArcMeta {

class DropListView : public QListView {
    Q_OBJECT
public:
    explicit DropListView(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

signals:
    void pathsDropped(const QStringList& paths, const QModelIndex& targetIndex);
};

} // namespace ArcMeta

#endif // DROPLISTVIEW_H
