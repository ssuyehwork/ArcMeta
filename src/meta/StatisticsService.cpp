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

    // 1. 提取所有属于 ③ 的用户自定义分类 ID 集合
    std::unordered_set<int> userCategoryIds;
    auto allCats = CategoryRepo::getCachedAll();
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::User && cat.id > 0) {
            userCategoryIds.insert(cat.id);
        }
    }

    // 2. 收集所有已归入 ③ 自定义分类的 folder_id（跨所有分库精准去重）
    std::unordered_set<std::string> categorizedFolderIds;
    if (!userCategoryIds.empty()) {
        QStringList placeholders;
        for (size_t i = 0; i < userCategoryIds.size(); ++i) placeholders << "?";
        QString sql = QString("SELECT DISTINCT folder_id FROM category_items WHERE category_id IN (%1)").arg(placeholders.join(","));
        QByteArray sqlUtf8 = sql.toUtf8();

        for (sqlite3* db : dbs) {
            if (!db) continue;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sqlUtf8.constData(), -1, &stmt, nullptr) == SQLITE_OK) {
                int bindIdx = 1;
                for (int cid : userCategoryIds) {
                    sqlite3_bind_int(stmt, bindIdx++, cid);
                }
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (fid) categorizedFolderIds.insert(fid);
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    // 3. 计算 "all"、"untagged"、"uncategorized"、"trash"
    int allCount = 0;
    int untaggedCount = 0;
    int uncategorizedCount = 0;
    int trashCount = 0;

    for (sqlite3* db : dbs) {
        if (!db) continue;

        // 3.1 统计有效资产（非回收站且非目录）
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT folder_id, tags FROM metadata WHERE is_trash = 0 AND is_folder = 0";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                allCount++;
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const wchar_t* tags = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));

                // 未标签判定
                if (!tags || wcslen(tags) == 0) {
                    untaggedCount++;
                }

                // 核心规则：未出现在 ③ 自定义分类关联中的，严格计入未分类
                if (fid && categorizedFolderIds.find(fid) == categorizedFolderIds.end()) {
                    uncategorizedCount++;
                }
            }
            sqlite3_finalize(stmt);
        }

        // 3.2 托管回收站总数
        sqlite3_stmt* stmtLib = nullptr;
        const char* sqlLib = "SELECT COUNT(DISTINCT folder_id) FROM metadata WHERE is_trash = 1";
        if (sqlite3_prepare_v2(db, sqlLib, -1, &stmtLib, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtLib) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtLib, 0);
            }
            sqlite3_finalize(stmtLib);
        }

        // 3.3 物理磁盘回收站总数
        sqlite3_stmt* stmtDisk = nullptr;
        const char* sqlDisk = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sqlDisk, -1, &stmtDisk, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmtDisk) == SQLITE_ROW) {
                trashCount += sqlite3_column_int(stmtDisk, 0);
            }
            sqlite3_finalize(stmtDisk);
        }
    }

    snapshot.systemCounts["all"] = allCount;
    snapshot.systemCounts["untagged"] = untaggedCount;
    snapshot.systemCounts["uncategorized"] = uncategorizedCount;
    snapshot.systemCounts["trash"] = trashCount;
    snapshot.systemCounts["tags"] = 0;
    snapshot.systemCounts["recently_visited"] = 0;

    // 4. 计算系统托管根分类分账（单盘有效资产总数）
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
                sqlite3_stmt* stmtLibCnt = nullptr;
                const char* sqlCnt = "SELECT COUNT(*) FROM metadata WHERE is_trash = 0 AND is_folder = 0";
                if (sqlite3_prepare_v2(targetDb, sqlCnt, -1, &stmtLibCnt, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(stmtLibCnt) == SQLITE_ROW) {
                        libCount = sqlite3_column_int(stmtLibCnt, 0);
                    }
                    sqlite3_finalize(stmtLibCnt);
                }
            }
            snapshot.libraryCounts[cat.id] = libCount;
        }
    }

    // 5. 计算 ③ 自定义分类分账（仅统计 is_trash = 0 且 is_folder = 0 的有效资产）
    for (const auto& cat : allCats) {
        if (cat.kind == CategoryKind::User && cat.id > 0) {
            int userCount = 0;
            for (sqlite3* db : dbs) {
                if (!db) continue;
                sqlite3_stmt* stmtUser = nullptr;
                const char* sqlUser =
                    "SELECT COUNT(DISTINCT ci.folder_id) FROM category_items ci "
                    "JOIN metadata m ON ci.folder_id = m.folder_id "
                    "WHERE ci.category_id = ? AND m.is_trash = 0 AND m.is_folder = 0";
                if (sqlite3_prepare_v2(db, sqlUser, -1, &stmtUser, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmtUser, 1, cat.id);
                    if (sqlite3_step(stmtUser) == SQLITE_ROW) {
                        userCount += sqlite3_column_int(stmtUser, 0);
                    }
                    sqlite3_finalize(stmtUser);
                }
            }
            snapshot.userCategoryCounts[cat.id] = userCount;
        }
    }

    // 6. 同步到内存原子变量
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
