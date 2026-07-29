#include "SystemBootstrapper.h"
#include "NativeFolderWatcher.h"
#include "../meta/MetadataManager.h"
#include <QDir>
#include <QDebug>

namespace ArcMeta {

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
                qDebug() << "[Boot] 点火托管库 IOCP 监控:" << QString::fromStdWString(managedAbsW);
                NativeFolderWatcher::instance().addWatch(managedAbsW);
            }
        }
    }
}

} // namespace ArcMeta
