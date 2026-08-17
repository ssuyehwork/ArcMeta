#pragma once

#include <QObject>
#include <QStringList>

namespace ArcMeta {

class SearchHistoryService : public QObject {
    Q_OBJECT
public:
    static SearchHistoryService& instance();

    QStringList getHistory(const QString& category) const;
    void appendSearch(const QString& category, const QString& keyword);
    void removeSearch(const QString& category, const QString& keyword);
    void clearAll(const QString& category);

signals:
    void searchHistoryChanged(const QString& category, const QStringList& newHistory);

private:
    SearchHistoryService(QObject* parent = nullptr);
    ~SearchHistoryService() override = default;

    const int m_maxLimit = 10;
};

} // namespace ArcMeta
