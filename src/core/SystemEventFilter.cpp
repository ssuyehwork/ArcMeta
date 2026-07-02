#include "SystemEventFilter.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <Dbt.h>
#endif

namespace ArcMeta {

SystemEventFilter::SystemEventFilter(QObject* parent) : QObject(parent) {
}

bool SystemEventFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(eventType);
    Q_UNUSED(result);

#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_DEVICECHANGE) {
        if (msg->wParam == DBT_DEVICEARRIVAL) {
            qDebug() << "[SystemEvent] 检测到外部设备插入 (DBT_DEVICEARRIVAL)";
            emit deviceChanged(true);
        } else if (msg->wParam == DBT_DEVICEREMOVECOMPLETE) {
            qDebug() << "[SystemEvent] 检测到外部设备移除 (DBT_DEVICEREMOVECOMPLETE)";
            emit deviceChanged(false);
        }
    }
#endif

    return false;
}

} // namespace ArcMeta
