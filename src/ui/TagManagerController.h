#pragma once
#include <QObject>
#include <QString>
#include <QtConcurrent>
#include "../meta/TagRepository.h"

namespace ArcMeta {
class TagManagerController : public QObject {
    Q_OBJECT
public:
    explicit TagManagerController(QObject* parent = nullptr) : QObject(parent) {}

    // 🚀 专职异步写库：后台线程写入，不引入 QWidget 等 UI 依赖
    void addTagToGroupAsync(const QString& tagName, int groupId) {
        (void)QtConcurrent::run([this, tagName, groupId]() {
            if (TagRepository::addTagToGroup(tagName, groupId)) {
                emit tagGroupStateChanged(); // 成功后发射刷新信号
            }
        });
    }

    void removeTagFromGroupAsync(const QString& tagName, int groupId = -1) {
        (void)QtConcurrent::run([this, tagName, groupId]() {
            if (TagRepository::removeTagFromGroup(tagName, groupId)) {
                emit tagGroupStateChanged();
            }
        });
    }
signals:
    void tagGroupStateChanged();
};
}
