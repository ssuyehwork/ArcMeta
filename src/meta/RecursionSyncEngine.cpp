#include "RecursionSyncEngine.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
#include "../mft/MftReader.h"
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

namespace ArcMeta {

RecursionSyncEngine& RecursionSyncEngine::instance() {
    static RecursionSyncEngine inst;
    return inst;
}

RecursionSyncEngine::RecursionSyncEngine(QObject* parent) : QObject(parent) {
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &RecursionSyncEngine::onMftEntryAdded);
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &RecursionSyncEngine::onMftEntryUpdated);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &RecursionSyncEngine::onMftEntryRemoved);

    m_usnPersistTimer = new QTimer(this);
    m_usnPersistTimer->setInterval(3000); // 3 秒持久化一次 USN
    connect(m_usnPersistTimer, &QTimer::timeout, [this]() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_contexts) {
            if (pair.second.pendingUsn > 0) {
                saveLastUsn(pair.second.db, pair.second.pendingUsn);
                pair.second.pendingUsn = 0;
            }
        }
    });
    m_usnPersistTimer->start();
}

RecursionSyncEngine::~RecursionSyncEngine() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_contexts.clear();
}

void RecursionSyncEngine::startSync(const std::wstring& volume, const std::wstring& volumeSerial, sqlite3* db) {
    std::lock_guard<std::mutex> lock(m_mutex);
    qDebug() << "[SyncEngine] Starting sync for volume:" << QString::fromStdWString(volume) << "Serial:" << QString::fromStdWString(volumeSerial);
    
    SyncContext ctx;
    ctx.volume = volume;
    ctx.db = db;

    // 预编译 SQL 语句以提升性能
    const char* updateSql = "INSERT OR REPLACE INTO metadata (file_id, path, is_folder, mtime, file_size) VALUES (?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(db, updateSql, -1, &ctx.updateStmt, nullptr);

    const char* deleteSql = "DELETE FROM metadata WHERE file_id = ?";
    sqlite3_prepare_v2(db, deleteSql, -1, &ctx.deleteStmt, nullptr);

    m_contexts[volumeSerial] = ctx;

    // 2026-07-xx 按照重构方案：从库中获取 LastUsn 并追平
    uint64_t lastUsn = getLastUsn(db);
    
    // 驱动生命周期与库挂载状态绑定
    MftReader::instance().startUsnWatcher(volume, lastUsn);
}

void RecursionSyncEngine::stopSync(const std::wstring& volumeSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_contexts.find(volumeSerial);
    if (it != m_contexts.end()) {
        qDebug() << "[SyncEngine] Stopping sync for serial:" << QString::fromStdWString(volumeSerial);
        std::wstring vol = it->second.volume;
        
        // 2026-07-xx 按照重构方案：退出递归时物理卸载 USN 驱动
        MftReader::instance().stopUsnWatcher(vol);
        
        if (it->second.updateStmt) sqlite3_finalize(it->second.updateStmt);
        if (it->second.deleteStmt) sqlite3_finalize(it->second.deleteStmt);
        
        m_contexts.erase(it);
    }
}

void RecursionSyncEngine::onMftEntryAdded(uint64_t compositeKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t driveIdx = static_cast<size_t>(compositeKey >> 48);
    std::wstring volume = MftReader::instance().getVolumeName(driveIdx);
    if (volume.empty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(volume);
    auto it = m_contexts.find(volSerial);
    if (it != m_contexts.end()) {
        updateDbEntry(it->second.db, compositeKey);
        it->second.pendingUsn = MftReader::instance().getNextUsn(volume);
    }
}

void RecursionSyncEngine::onMftEntryUpdated(uint64_t compositeKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t driveIdx = static_cast<size_t>(compositeKey >> 48);
    std::wstring volume = MftReader::instance().getVolumeName(driveIdx);
    if (volume.empty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(volume);
    auto it = m_contexts.find(volSerial);
    if (it != m_contexts.end()) {
        updateDbEntry(it->second.db, compositeKey);
        it->second.pendingUsn = MftReader::instance().getNextUsn(volume);
    }
}

void RecursionSyncEngine::onMftEntryRemoved(uint64_t compositeKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t driveIdx = static_cast<size_t>(compositeKey >> 48);
    std::wstring volume = MftReader::instance().getVolumeName(driveIdx);
    if (volume.empty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(volume);
    auto it = m_contexts.find(volSerial);
    if (it != m_contexts.end()) {
        removeDbEntry(it->second.db, compositeKey);
        it->second.pendingUsn = MftReader::instance().getNextUsn(volume);
    }
}

void RecursionSyncEngine::updateDbEntry(sqlite3* db, uint64_t compositeKey) {
    if (!db) return;
    int idx = MftReader::instance().getIndexByKey(compositeKey);
    if (idx == -1) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring path = qPath.toStdWString();
    std::wstring vol = MetadataManager::getVolumeSerialNumber(path);
    auto it = m_contexts.find(vol);
    if (it == m_contexts.end() || !it->second.updateStmt) return;

    sqlite3_stmt* stmt = it->second.updateStmt;
    
    uint64_t frn = MftReader::instance().getFrn(idx);
    wchar_t frnBuf[17];
    swprintf(frnBuf, 17, L"%016llX", frn);
    std::string fid = MetadataManager::generateFallbackFid(vol, frnBuf);

    bool isFolder = MftReader::instance().isDirectory(idx);
    long long size = MftReader::instance().getSize(idx);
    long long mtime = MftReader::instance().getModifyTime(idx);
    
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, isFolder ? 1 : 0);
    sqlite3_bind_int64(stmt, 4, mtime);
    sqlite3_bind_int64(stmt, 5, size);
    sqlite3_step(stmt);
}

void RecursionSyncEngine::removeDbEntry(sqlite3* db, uint64_t compositeKey) {
    if (!db) return;
    size_t driveIdx = static_cast<size_t>(compositeKey >> 48);
    std::wstring volume = MftReader::instance().getVolumeName(driveIdx);
    if (volume.empty()) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(volume);
    auto it = m_contexts.find(volSerial);
    if (it == m_contexts.end() || !it->second.deleteStmt) return;

    sqlite3_stmt* stmt = it->second.deleteStmt;

    uint64_t frn = compositeKey & 0x0000FFFFFFFFFFFFull;
    wchar_t frnBuf[17];
    swprintf(frnBuf, 17, L"%016llX", frn);
    std::string fid = MetadataManager::generateFallbackFid(volSerial, frnBuf);
    
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
}

uint64_t RecursionSyncEngine::getLastUsn(sqlite3* db) {
    uint64_t usn = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT value FROM system_stats WHERE key = 'last_usn'", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            usn = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return usn;
}

void RecursionSyncEngine::saveLastUsn(sqlite3* db, uint64_t usn) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES ('last_usn', ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, usn);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

} // namespace ArcMeta
