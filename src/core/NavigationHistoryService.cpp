#include "NavigationHistoryService.h"
#include "AppConfig.h"
#include "../meta/MetadataManager.h"

namespace QuarkMeta {

NavigationHistoryService& NavigationHistoryService::instance() {
    static NavigationHistoryService inst;
    return inst;
}

NavigationHistoryService::NavigationHistoryService(QObject* parent) : QObject(parent) {}

QStringList NavigationHistoryService::getHistory() const {
    return AppConfig::instance().getValue("AddressBar/History").toStringList();
}

void NavigationHistoryService::appendPath(const QString& path) {
    if (path.isEmpty() || path == "computer://" || path.startsWith("分类: ")) return;
    QStringList history = getHistory();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > m_maxLimit) {
        history.removeLast();
    }
    AppConfig::instance().setValue("AddressBar/History", history);
    AppConfig::instance().sync();
    emit historyChanged(history);
}

void NavigationHistoryService::removePath(const QString& path) {
    QStringList history = getHistory();
    history.removeAll(path);
    AppConfig::instance().setValue("AddressBar/History", history);
    AppConfig::instance().sync();
    emit historyChanged(history);
}

void NavigationHistoryService::clearAll() {
    AppConfig::instance().setValue("AddressBar/History", QStringList());
    AppConfig::instance().sync();
    emit historyChanged(QStringList());
}

void NavigationHistoryService::recordRecentVisitedFolder(const std::wstring& path) {
    if (path.empty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return;

    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    QStringList list = AppConfig::instance().getValue(key, QStringList()).toStringList();

    QString qPath = QString::fromStdWString(MetadataManager::normalizePath(path));
    list.removeAll(qPath);
    list.prepend(qPath);
    while (list.size() > 14) list.removeLast();

    AppConfig::instance().setValue(key, list);
}

QStringList NavigationHistoryService::getRecentVisitedFolders(const std::wstring& volSerial) {
    if (volSerial.empty()) return QStringList();
    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    return AppConfig::instance().getValue(key, QStringList()).toStringList();
}

} // namespace QuarkMeta
