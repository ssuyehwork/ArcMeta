#ifndef ACTIONRECORDER_H
#define ACTIONRECORDER_H

#include <QObject>
#include <QStringList>
#include <QVariant>

class ActionRecorder : public QObject {
    Q_OBJECT

public:
    enum class ActionType {
        None,
        PasteTags,
        MoveToCategory
    };

    static ActionRecorder& instance();

    // 记录行为
    void recordPasteTags(const QStringList& tags);
    void recordMoveToCategory(int categoryId);

    // 获取当前记录
    ActionType getLastActionType() const;
    QVariant getLastActionData() const;

private:
    ActionRecorder(QObject* parent = nullptr);
    ~ActionRecorder() = default;

    ActionRecorder(const ActionRecorder&) = delete;
    ActionRecorder& operator=(const ActionRecorder&) = delete;

    ActionType m_lastActionType;
    QVariant m_lastActionData;
};

#endif // ACTIONRECORDER_H
