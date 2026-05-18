#ifndef MESSAGECAPTUREHANDLER_H
#define MESSAGECAPTUREHANDLER_H

#include <QObject>

class MessageCaptureHandler : public QObject {
    Q_OBJECT
public:
    static MessageCaptureHandler& instance();
    void init();

private slots:
    void onEnterPressed(bool ctrl, bool shift, bool alt);

private:
    MessageCaptureHandler(QObject* parent = nullptr);
    qint64 m_lastTriggerTime = 0;
};

#endif // MESSAGECAPTUREHANDLER_H
