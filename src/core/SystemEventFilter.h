#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace ArcMeta {

/**
 * @brief 系统原生事件过滤器
 * 2026-07-xx 按照 Analysis_Modification_Plan-120：剥离 MainWindow 的硬件消息耦合
 * 专门拦截 WM_DEVICECHANGE 等系统级底层消息。
 */
class SystemEventFilter : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit SystemEventFilter(QObject* parent = nullptr);

    /**
     * @brief 实现原生事件拦截接口
     */
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    /**
     * @brief 硬件设备状态发生变更信号
     * @param arrival true 为设备插入, false 为设备移除
     */
    void deviceChanged(bool arrival);
};

} // namespace ArcMeta
