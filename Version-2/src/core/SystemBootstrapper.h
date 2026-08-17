#ifndef ARCMETA_SYSTEM_BOOTSTRAPPER_H
#define ARCMETA_SYSTEM_BOOTSTRAPPER_H

#include <QObject>

namespace ArcMeta {

/**
 * @brief 专职负责底盘级硬件热插拔、IOCP 多维监控智能点火的纯无头（Headless）服务类
 */
class SystemBootstrapper : public QObject {
    Q_OBJECT
public:
    static SystemBootstrapper& instance();

    /**
     * @brief 驱动多盘符资源库并开启底层 NativeFolderWatcher IOCP 监控 (从 MainWindow 移出)
     */
    void bootstrapMonitors();

private:
    SystemBootstrapper(QObject* parent = nullptr);
    ~SystemBootstrapper() override = default;
};

} // namespace ArcMeta

#endif // ARCMETA_SYSTEM_BOOTSTRAPPER_H
