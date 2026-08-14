#include "StatisticsService.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include <QThreadPool>
#include <QRunnable>
#include <QCoreApplication>

namespace ArcMeta {

class RecountTask : public QRunnable {
public:
    RecountTask(std::function<void(const StatisticsSnapshot&)> callback)
        : m_callback(callback) {}

    void run() override {
        StatisticsSnapshot snapshot = StatisticsService::instance().computeSnapshotFromDb();
        QMetaObject::invokeMethod(&StatisticsService::instance(), [snapshot, cb = m_callback]() {
            if (cb) cb(snapshot);
            emit StatisticsService::instance().statisticsUpdated(snapshot);
        });
    }

private:
    std::function<void(const StatisticsSnapshot&)> m_callback;
};

StatisticsService& StatisticsService::instance() {
    static StatisticsService inst;
    return inst;
}

StatisticsService::StatisticsService(QObject* parent)
    : QObject(parent) {}

StatisticsSnapshot StatisticsService::getCachedSnapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    return m_cachedSnapshot;
}

void StatisticsService::requestFullRecountAsync(std::function<void(const StatisticsSnapshot&)> callback) {
    QThreadPool::globalInstance()->start(new RecountTask(callback));
}

void StatisticsService::notifyAssetAdded(int targetCatId, bool hasTags) {
    m_totalCount.fetch_add(1);
    if (targetCatId <= 0) {
        m_uncategorizedCount.fetch_add(1);
    }
    if (!hasTags) {
        m_untaggedCount.fetch_add(1);
    }

    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_cachedSnapshot.systemCounts["all"] = m_totalCount.load();
    m_cachedSnapshot.systemCounts["uncategorized"] = m_uncategorizedCount.load();
    m_cachedSnapshot.systemCounts["untagged"] = m_untaggedCount.load();
    if (targetCatId > 0) {
        m_cachedSnapshot.userCategoryCounts[targetCatId]++;
    }

    emit statisticsUpdated(m_cachedSnapshot);
}

void StatisticsService::notifyAssetRemoved(int targetCatId, bool hadTags, bool wasTrash) {
    if (wasTrash) {
        m_trashCount.fetch_sub(1);
    } else {
        m_totalCount.fetch_sub(1);
        if (targetCatId <= 0) {
            m_uncategorizedCount.fetch_sub(1);
        }
        if (!hadTags) {
            m_untaggedCount.fetch_sub(1);
        }
    }

    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_cachedSnapshot.systemCounts["all"] = m_totalCount.load();
    m_cachedSnapshot.systemCounts["uncategorized"] = m_uncategorizedCount.load();
    m_cachedSnapshot.systemCounts["untagged"] = m_untaggedCount.load();
    m_cachedSnapshot.systemCounts["trash"] = m_trashCount.load();
    if (targetCatId > 0 && !wasTrash) {
        if (m_cachedSnapshot.userCategoryCounts[targetCatId] > 0) {
            m_cachedSnapshot.userCategoryCounts[targetCatId]--;
        }
    }

    emit statisticsUpdated(m_cachedSnapshot);
}

void StatisticsService::notifyAssetTrashChanged(bool toTrash, bool hasTags) {
    if (toTrash) {
        m_totalCount.fetch_sub(1);
        m_trashCount.fetch_add(1);
        if (!hasTags) m_untaggedCount.fetch_sub(1);
    } else {
        m_totalCount.fetch_add(1);
        m_trashCount.fetch_sub(1);
        if (!hasTags) m_untaggedCount.fetch_add(1);
    }

    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_cachedSnapshot.systemCounts["all"] = m_totalCount.load();
    m_cachedSnapshot.systemCounts["trash"] = m_trashCount.load();
    m_cachedSnapshot.systemCounts["untagged"] = m_untaggedCount.load();

    emit statisticsUpdated(m_cachedSnapshot);
}

void StatisticsService::notifyDiskTrashCountChanged(int delta) {
    m_trashCount.fetch_add(delta);

    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_cachedSnapshot.systemCounts["trash"] = m_trashCount.load();

    emit statisticsUpdated(m_cachedSnapshot);
}

StatisticsSnapshot StatisticsService::computeSnapshotFromDb() {
    StatisticsSnapshot snapshot;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

    // 1. "all"：所有库中 is_trash = 0 且 is_folder = 0 的素材总和；
    int allCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                allCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["all"] = allCount;

    // 2. "trash"：各库 is_trash = 1 的总和 + 各盘 disk_trash 表记录总和；
    int trashCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmtLib = nullptr;
        const char* sqlLib = "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 1";
        if (sqlite3_prepare_v2(db, sqlLib, -1, &stmtLib, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtLib) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtLib, 0);
            }
            sqlite3_finalize(stmtLib);
        }
        sqlite3_stmt* stmtDisk = nullptr;
        const char* sqlDisk = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sqlDisk, -1, &stmtDisk, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtDisk) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtDisk, 0);
            }
            sqlite3_finalize(stmtDisk);
        }
    }
    snapshot.systemCounts["trash"] = trashCount;

    // 3. "untagged"：所有库中 is_trash = 0 且 (tags IS NULL OR tags = '') 的素材总和；
    int untaggedCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND (tags IS NULL OR tags = '')";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                untaggedCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["untagged"] = untaggedCount;

    // 4. "uncategorized"：所有库中 is_trash = 0 且其 folder_id 从未出现在任何用户自定义分类（category_kind == 0）关联中的素材总和。
    int uncategorizedCount = 0;
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 0 AND is_folder = 0 AND folder_id NOT IN ("
            "    SELECT ci.folder_id FROM category_items ci "
            "    JOIN categories c ON ci.category_id = c.id "
            "    WHERE c.category_kind = 0"
            ")";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                uncategorizedCount += sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    snapshot.systemCounts["uncategorized"] = uncategorizedCount;

    snapshot.systemCounts["tags"] = 0;
    snapshot.systemCounts["recently_visited"] = 0;

    auto allCats = CategoryRepo::getCachedAll();
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::SystemLibrary) {
            QString rawPath = QString::fromStdWString(cat.physicalPath);
            sqlite3* targetDb = nullptr;

            if (rawPath.length() >= 2 && rawPath[1] == ':') {
                QString letter = rawPath.left(1).toUpper();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(rawPath.left(3).toStdWString());
                if (volSerial != L"UNKNOWN") {
                    targetDb = DatabaseManager::instance().getDriveDb(volSerial, letter);
                }
            }
            if (!targetDb) {
                targetDb = DatabaseManager::instance().getDbForPath(cat.physicalPath);
            }

            int libCount = 0;
            if (targetDb) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0";
                if (sqlite3_prepare_v2(targetDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        libCount = sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
            }
            snapshot.libraryCounts[cat.id] = libCount;
        }
    }

    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::User) {
            int userCount = 0;
            for (sqlite3* db : dbs) {
                if (!db) continue;
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT COUNT(DISTINCT ci.folder_id) FROM category_items ci "
                                  "JOIN metadata m ON ci.folder_id = m.folder_id "
                                  "WHERE ci.category_id = ? AND m.is_trash = 0 AND m.is_folder = 0";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, cat.id);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        userCount += sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);
                }
            }
            snapshot.userCategoryCounts[cat.id] = userCount;
        }
    }

    // 同步到内存原子变量
    m_totalCount.store(allCount);
    m_uncategorizedCount.store(uncategorizedCount);
    m_untaggedCount.store(untaggedCount);
    m_trashCount.store(trashCount);

    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_cachedSnapshot = snapshot;
    }

    return snapshot;
}

} // namespace ArcMeta
