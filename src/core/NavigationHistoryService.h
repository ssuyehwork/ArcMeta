#pragma once

#include <QObject>
#include <QStringList>

namespace ArcMeta {

class NavigationHistoryService : public QObject {
    Q_OBJECT
public:
    static NavigationHistoryService& instance();

    QStringList getHistory() const;
    void appendPath(const QString& path);
    void removePath(const QString& path);
    void clearAll();

    // 2026-07-xx 按照 Plan-119：记录与获取最近访问文件夹 (从 AutoImportManager 解耦迁移至此)
    static void recordRecentVisitedFolder(const std::wstring& path);
    static QStringList getRecentVisitedFolders(const std::wstring& volSerial);

    // 协议栈压栈、出栈和边界判定 (Plan-111 解耦重构)
    void recordNavigation(const QString& url, bool record = true);
    bool canGoBack() const;
    bool canGoForward() const;
    QString goBack();
    QString goForward();
    QString currentUrl() const;

signals:
    void historyChanged(const QStringList& newHistory);

private:
    NavigationHistoryService(QObject* parent = nullptr);
    ~NavigationHistoryService() override = default;

    const int m_maxLimit = 15;

    QStringList m_navHistory;
    int m_navHistoryIndex = -1;
};

} // namespace ArcMeta
