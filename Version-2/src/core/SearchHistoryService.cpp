#include "SearchHistoryService.h"
#include "AppConfig.h"

namespace ArcMeta {

SearchHistoryService& SearchHistoryService::instance() {
    static SearchHistoryService inst;
    return inst;
}

SearchHistoryService::SearchHistoryService(QObject* parent) : QObject(parent) {}

QStringList SearchHistoryService::getHistory(const QString& category) const {
    QString configKey = (category == "global") ? "Search/History" : QString("SearchHistory/%1").arg(category);
    return AppConfig::instance().getValue(configKey).toStringList();
}

void SearchHistoryService::appendSearch(const QString& category, const QString& keyword) {
    if (keyword.isEmpty()) return;
    QString configKey = (category == "global") ? "Search/History" : QString("SearchHistory/%1").arg(category);
    QStringList history = getHistory(category);
    history.removeAll(keyword);
    history.prepend(keyword);
    while (history.size() > m_maxLimit) {
        history.removeLast();
    }
    AppConfig::instance().setValue(configKey, history);
    AppConfig::instance().sync();
    emit searchHistoryChanged(category, history);
}

void SearchHistoryService::removeSearch(const QString& category, const QString& keyword) {
    QString configKey = (category == "global") ? "Search/History" : QString("SearchHistory/%1").arg(category);
    QStringList history = getHistory(category);
    history.removeAll(keyword);
    AppConfig::instance().setValue(configKey, history);
    AppConfig::instance().sync();
    emit searchHistoryChanged(category, history);
}

void SearchHistoryService::clearAll(const QString& category) {
    QString configKey = (category == "global") ? "Search/History" : QString("SearchHistory/%1").arg(category);
    AppConfig::instance().setValue(configKey, QStringList());
    AppConfig::instance().sync();
    emit searchHistoryChanged(category, QStringList());
}

} // namespace ArcMeta
