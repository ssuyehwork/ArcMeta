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

signals:
    void historyChanged(const QStringList& newHistory);

private:
    NavigationHistoryService(QObject* parent = nullptr);
    ~NavigationHistoryService() override = default;

    const int m_maxLimit = 15;
};

} // namespace ArcMeta
