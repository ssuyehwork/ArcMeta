#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QReadWriteLock>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include <windows.h>
#include <winioctl.h>
#include <QIcon>
#include <QHash>
#include "MftDataStore.h"
#include "ScchCache.h"

namespace ArcMeta {

class SyncManager;

/**
 * @brief 高性能 MFT 索引引擎 (Facade 模式)
 */
class MftReader : public QObject {
    Q_OBJECT
public:
    static MftReader& instance();
    QIcon getCachedIcon(const QString& ext, bool isDir);

signals:
    void dataChanged(int index = -1);
    void entryAdded(uint32_t index);
    void entryRemoved(uint64_t keyLow);
    void entryUpdated(uint32_t index);
    void driveLoaded(const QString& drive, int count, int total);

public:
    void buildIndex(const QStringList& drives = QStringList());
    bool loadFromCache();
    bool saveToCache(); 
    bool saveDriveToCache(size_t driveIdx); 
    void clear();

    void updateActiveDrives(const QStringList& activeDrives);
    bool isDriveIndexed(const QString& drive);

    std::vector<uint64_t> search(const QString& query, bool useRegex = false, bool caseSensitive = false, 
                                 const QStringList& extensionList = QStringList(), 
                                 bool includeHidden = true, bool includeSystem = true,
                                 bool includeDollar = true);
    
    bool     matchEntry(int index, const QString& query, bool useRegex, bool caseSensitive, 
                        const QStringList& extensionList, bool includeHidden, bool includeSystem,
                        bool includeDollar = true) const;

    int      getIndexByKey(uint32_t driveIdx, Frn128 frn) const;
    int      getIndexByKey(uint64_t compositeKey) const;
    QString  getName(int index) const;
    int64_t  getSize(int index) const;
    int64_t  getModifyTime(int index) const;
    uint32_t getAttributes(int index) const;
    Frn128   getFrn(int index) const;
    bool     isDirectory(int index) const;
    int      totalCount() const;
    QString  getFullPath(int index) const;
    void     requestMetadata(int index);
    bool     isMetadataFetched(int index) const;

    std::wstring getPathFast(uint32_t driveIdx, Frn128 frn);

private slots:
    void onUsnRecordReceived(const std::wstring& volume, const QByteArray& recordData);
    void onJournalInvalidated(const std::wstring& volume);

private:
    MftReader();
    ~MftReader();

    void updateEntryFromUsnRecord(const QByteArray& recordData, const std::wstring& volume);
    void removeEntryByFrn(const std::wstring& volume, Frn128 frn);
    bool saveDriveToCacheInternal(size_t driveIdx);
    void performBackgroundCompact();

    std::shared_ptr<MftDataStore> m_data;
    mutable QReadWriteLock m_dataLock;
    SyncManager* m_syncMgr;

    std::vector<std::wstring> m_drive_list;
    std::atomic<uint32_t>     m_drive_active_mask{0};
    std::unordered_map<std::wstring, uint64_t> m_next_usns;

    mutable std::unordered_map<uint64_t, std::wstring>  m_path_cache;
    mutable std::mutex m_pathCacheMutex;

    mutable QReadWriteLock m_iconCacheLock;
    QHash<QString, QIcon>  m_icon_cache;

    bool m_isInitialized = false;
    uint32_t m_dirty_count = 0;
};

} // namespace ArcMeta
