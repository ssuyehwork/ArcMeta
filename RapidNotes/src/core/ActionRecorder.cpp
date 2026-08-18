#include "ActionRecorder.h"

ActionRecorder& ActionRecorder::instance() {
    static ActionRecorder inst;
    return inst;
}

ActionRecorder::ActionRecorder(QObject* parent) 
    : QObject(parent), 
      m_lastActionType(ActionType::None) 
{
}

void ActionRecorder::recordPasteTags(const QStringList& tags) {
    m_lastActionType = ActionType::PasteTags;
    m_lastActionData = QVariant::fromValue(tags);
}

void ActionRecorder::recordMoveToCategory(int categoryId) {
    m_lastActionType = ActionType::MoveToCategory;
    m_lastActionData = QVariant::fromValue(categoryId);
}

ActionRecorder::ActionType ActionRecorder::getLastActionType() const {
    return m_lastActionType;
}

QVariant ActionRecorder::getLastActionData() const {
    return m_lastActionData;
}
