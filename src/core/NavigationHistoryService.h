#pragma once

#include <QObject>
#include <QStringList>

namespace QuarkMeta {

class NavigationHistoryService : public QObject {
    Q_OBJECT
public:
    static NavigationHistoryService& instance();

    QStringList getHistory() const;
    void appendPath(const QString& path);
    void removePath(const QString& path);
    void clearAll();

    // 2026-07-xx 按照 Plan-119：记录与获取最近访问文件夹
    static void recordRecentVisitedFolder(const std::wstring& path);
    static QStringList getRecentVisitedFolders(const std::wstring& volSerial);

signals:
    void historyChanged(const QStringList& newHistory);

private:
    NavigationHistoryService(QObject* parent = nullptr);
    ~NavigationHistoryService() override = default;

    const int m_maxLimit = 15;
};

} // namespace QuarkMeta
