#ifndef HOTKEYMANAGER_H
#define HOTKEYMANAGER_H

#include <QObject>
#include <QAbstractNativeEventFilter>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static HotkeyManager& instance();
    
    bool registerHotkey(int id, uint modifiers, uint vk);
    void unregisterHotkey(int id);
    void reapplyHotkeys();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void hotkeyPressed(int id);

private:
    HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager();
};

#endif // HOTKEYMANAGER_H