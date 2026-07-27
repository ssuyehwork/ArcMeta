#include "NavigationHistoryService.h"
#include "AppConfig.h"
#include "AutoImportManager.h"
#include "../meta/MetadataManager.h"

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

void NavigationHistoryService::recordRecentVisitedFolder(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring managedAbs = AutoImportManager::getManagedLibraryPath(path);
    if (!managedAbs.empty() && path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        return; // 在托管库内部，不作为物理最近文件夹记录
    }

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

void NavigationHistoryService::recordNavigation(const QString& url, bool record) {
    if (url.isEmpty()) return;
    if (record) {
        if (m_navHistoryIndex < static_cast<int>(m_navHistory.size()) - 1) {
            m_navHistory = m_navHistory.mid(0, m_navHistoryIndex + 1);
        }
        if (m_navHistory.isEmpty() || m_navHistory.last() != url) {
            m_navHistory.append(url);
            m_navHistoryIndex = static_cast<int>(m_navHistory.size()) - 1;
        }
    }
}

bool NavigationHistoryService::canGoBack() const {
    return m_navHistoryIndex > 0;
}

bool NavigationHistoryService::canGoForward() const {
    return m_navHistoryIndex < static_cast<int>(m_navHistory.size()) - 1;
}

QString NavigationHistoryService::goBack() {
    if (canGoBack()) {
        m_navHistoryIndex--;
        return m_navHistory[m_navHistoryIndex];
    }
    return QString();
}

QString NavigationHistoryService::goForward() {
    if (canGoForward()) {
        m_navHistoryIndex++;
        return m_navHistory[m_navHistoryIndex];
    }
    return QString();
}

QString NavigationHistoryService::currentUrl() const {
    if (m_navHistoryIndex >= 0 && m_navHistoryIndex < m_navHistory.size()) {
        return m_navHistory[m_navHistoryIndex];
    }
    return QString();
}

} // namespace ArcMeta
