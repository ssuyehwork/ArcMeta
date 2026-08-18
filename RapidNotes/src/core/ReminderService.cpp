#include "ReminderService.h"
#include <QDebug>

ReminderService& ReminderService::instance() {
    static ReminderService inst;
    return inst;
}

ReminderService::ReminderService(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ReminderService::checkReminders);
}

void ReminderService::start() {
    if (!m_timer->isActive()) {
        // [PROFESSIONAL] 为了支持秒级/分级重复提醒，将检查频率提升至 1 秒
        m_timer->start(1000); 
        checkReminders();
    }
}

void ReminderService::stop() {
    m_timer->stop();
}

void ReminderService::checkReminders() {
    QDateTime now = QDateTime::currentDateTime();
    
    // 如果跨天，清空已提醒列表
    if (m_lastCheck.date() != now.date()) {
        m_notifiedIds.clear();
    }
    m_lastCheck = now;

    // 获取所有待办事项（通常挂起的不会太多，直接获取所有待办进行过滤比较简单）
    QList<DatabaseManager::Todo> pending = DatabaseManager::instance().getAllPendingTodos();
    
    for (const auto& todo : pending) {
        if (!todo.reminderTime.isValid()) continue;
        if (m_notifiedIds.contains(todo.id)) continue;

        // 如果提醒时间在当前时间之前（或当前分钟内），且未过期太久（如10分钟内）
        qint64 diff = todo.reminderTime.secsTo(now);
        if (diff >= 0 && diff < 600) {
            m_notifiedIds.insert(todo.id);
            emit todoReminderTriggered(todo);
        } else if (diff >= 600 && todo.status == 0) {
            // 如果超过10分钟还没处理，且状态还是待办，根据逻辑可以标记为逾期（此处根据需求推导）
            // DatabaseManager::instance().updateTodoStatus(todo.id, 2); // 逾期
        }
    }
}
