#ifndef DROPTREEVIEW_H
#define DROPTREEVIEW_H

#include <QTreeView>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

class DropTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit DropTreeView(QWidget* parent = nullptr);

signals:
    void notesDropped(const QList<int>& noteIds, const QModelIndex& targetIndex);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

    // [COMPAT] 2026-03-xx 兼容性修复：重写键盘搜索逻辑
    // 通过将此函数留空，可以彻底禁用 QTreeView 的“按名搜索”功能，
    // 同时避免使用 Qt 6.3+ 才支持的 setKeyboardSearchEnabled 导致的编译错误。
    void keyboardSearch(const QString& search) override;
};

#endif // DROPTREEVIEW_H
