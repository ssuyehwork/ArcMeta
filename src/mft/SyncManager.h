#pragma once

#include <QObject>
#include <QStringList>
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace ArcMeta {

class UsnWatcher;

/**
 * @brief 任务调度层：管理全量扫描与增量同步的生命周期
 */
class SyncManager : public QObject {
    Q_OBJECT
public:
    explicit SyncManager(QObject* parent = nullptr);
    ~SyncManager();

    void startWatching(const std::wstring& volume, uint64_t startUsn);
    void stopWatching(const std::wstring& volume);
    void stopAll();

signals:
    void usnRecordReceived(const std::wstring& volume, const QByteArray& recordData);
    void journalInvalidated(const std::wstring& volume);

private:
    std::map<std::wstring, UsnWatcher*> m_watchers;
};

} // namespace ArcMeta
