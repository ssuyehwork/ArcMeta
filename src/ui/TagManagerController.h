#pragma once
#include <QObject>
#include <QString>

namespace ArcMeta {

class TagManagerController : public QObject {
    Q_OBJECT
public:
    explicit TagManagerController(QObject* parent = nullptr);

    // 🚀 专职异步写库：后台线程写入，不引入 QWidget 等 UI 依赖
    void addTagToGroupAsync(const QString& tagName, int groupId);
    void removeTagFromGroupAsync(const QString& tagName, int groupId = -1);

signals:
    void tagGroupStateChanged();
};

} // namespace ArcMeta
