#include "SystemBootstrapper.h"
#include "NativeFolderWatcher.h"
#include "../meta/MetadataManager.h"
#include <QDir>
#include <QDebug>

namespace QuarkMeta {

SystemBootstrapper& SystemBootstrapper::instance() {
    static SystemBootstrapper inst;
    return inst;
}

SystemBootstrapper::SystemBootstrapper(QObject* parent) : QObject(parent) {}

void SystemBootstrapper::bootstrapMonitors() {
    qDebug() << "[Boot] SystemBootstrapper 开始点火底层 IOCP 监控...";
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        std::wstring wPath = d.absolutePath().toStdWString();
        std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wPath);
        QString letter = d.absolutePath().left(1).toUpper();

        if (volSerial != L"UNKNOWN") {
            std::wstring managedAbsW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (!managedAbsW.empty()) {
                qDebug() << "[Boot] 资源库无需点火 IOCP 监控（已取消）:" << QString::fromStdWString(managedAbsW);
                // 取消监控 QuarkMeta.Library_[盘符] 文件夹
                // NativeFolderWatcher::instance().addWatch(managedAbsW);
            }
        }
    }
}

} // namespace QuarkMeta
