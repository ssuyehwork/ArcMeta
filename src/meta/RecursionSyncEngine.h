#ifndef ARCMETA_RECURSION_SYNC_ENGINE_H
#define ARCMETA_RECURSION_SYNC_ENGINE_H

#include <QObject>
#include <QString>
#include <map>
#include <string>
#include <mutex>
#include <vector>
#include "sqlite3.h"

namespace ArcMeta {

class UsnWatcher;

/**
 * @brief 递归同步引擎：负责卷级 Recursion DB 的 USN 增量更新
 */
class RecursionSyncEngine : public QObject {
    Q_OBJECT
public:
    static RecursionSyncEngine& instance();

    /**
     * @brief 启动特定卷的同步
     * @param volume 卷路径（如 L"C:"）
     * @param volumeSerial 卷序列号
     * @param db 数据库句柄
     */
    void startSync(const std::wstring& volume, const std::wstring& volumeSerial, sqlite3* db);

    /**
     * @brief 停止特定卷的同步
     * @param volumeSerial 卷序列号
     */
    void stopSync(const std::wstring& volumeSerial);

private slots:
    void onMftEntryAdded(uint64_t compositeKey);
    void onMftEntryUpdated(uint64_t compositeKey);
    void onMftEntryRemoved(uint64_t compositeKey);

private:
    RecursionSyncEngine(QObject* parent = nullptr);
    ~RecursionSyncEngine();

    struct SyncContext {
        std::wstring volume;
        sqlite3* db = nullptr;
        sqlite3_stmt* updateStmt = nullptr;
        sqlite3_stmt* deleteStmt = nullptr;
        uint64_t pendingUsn = 0;
    };

    std::map<std::wstring, SyncContext> m_contexts;
    std::mutex m_mutex;
    QTimer* m_usnPersistTimer = nullptr;

    void updateDbEntry(sqlite3* db, uint64_t compositeKey);
    void removeDbEntry(sqlite3* db, uint64_t compositeKey);
    uint64_t getLastUsn(sqlite3* db);
    void saveLastUsn(sqlite3* db, uint64_t usn);
};

} // namespace ArcMeta

#endif // ARCMETA_RECURSION_SYNC_ENGINE_H
