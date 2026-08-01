#include "TagManagerController.h"
#include <QtConcurrent>
#include "../meta/TagRepository.h"

namespace ArcMeta {

TagManagerController::TagManagerController(QObject* parent) : QObject(parent) {}

void TagManagerController::addTagToGroupAsync(const QString& tagName, int groupId) {
    (void)QtConcurrent::run([this, tagName, groupId]() {
        if (TagRepository::addTagToGroup(tagName, groupId)) {
            emit tagGroupStateChanged(); // 成功后发射刷新信号
        }
    });
}

void TagManagerController::removeTagFromGroupAsync(const QString& tagName, int groupId) {
    (void)QtConcurrent::run([this, tagName, groupId]() {
        if (TagRepository::removeTagFromGroup(tagName, groupId)) {
            emit tagGroupStateChanged();
        }
    });
}

} // namespace ArcMeta
