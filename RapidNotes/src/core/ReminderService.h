#ifndef REMINDERSERVICE_H
#define REMINDERSERVICE_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QSet>
#include "DatabaseManager.h"

class ReminderService : public QObject {
    Q_OBJECT
public:
    static ReminderService& instance();

    void start();
    void stop();
    void removeNotifiedId(int id) { m_notifiedIds.remove(id); }

signals:
    void todoReminderTriggered(const DatabaseManager::Todo& todo);

private slots:
    void checkReminders();

private:
    ReminderService(QObject* parent = nullptr);
    ~ReminderService() = default;

    QTimer* m_timer;
    QSet<int> m_notifiedIds; // 避免在同一分钟内重复提醒
    QDateTime m_lastCheck;
};

#endif // REMINDERSERVICE_H
