#ifndef ARCMETA_CONTENT_CONTEXT_MENU_MANAGER_H
#define ARCMETA_CONTENT_CONTEXT_MENU_MANAGER_H

#include <QObject>
#include <QPoint>

namespace ArcMeta {

class ContentPanel;

class ContentContextMenuManager : public QObject {
    Q_OBJECT
public:
    static ContentContextMenuManager& instance();

    void showContextMenu(ContentPanel* panel, const QPoint& pos);

private:
    ContentContextMenuManager(QObject* parent = nullptr);
    ~ContentContextMenuManager() override = default;
};

} // namespace ArcMeta

#endif // ARCMETA_CONTENT_CONTEXT_MENU_MANAGER_H
