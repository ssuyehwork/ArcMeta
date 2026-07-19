#include "NavigationHistoryService.h"
#include "AppConfig.h"

namespace ArcMeta {

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

} // namespace ArcMeta
