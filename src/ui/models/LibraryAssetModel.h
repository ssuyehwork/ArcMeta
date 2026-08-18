#ifndef LIBRARYASSETMODEL_H
#define LIBRARYASSETMODEL_H

#include "ItemModelBase.h"
#include <QCache>
#include <QMap>
#include <QIcon>

#include <unordered_map>
#include <QSet>
#include "../../../src/meta/MetadataManager.h" // 引入必要的 RuntimeMeta

class LibraryAssetModel : public ItemModelBase {
    Q_OBJECT
public:
    explicit LibraryAssetModel(QObject* parent = nullptr);
    virtual ~LibraryAssetModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    const std::vector<QuarkMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
    void setRecords(const std::vector<QuarkMeta::ItemRecord>& records) override;
    void clear() override;
    void setQuery(const QString& query) override { m_query = query; }
    void updateRecordMetadata(const QString& path) override;
    void loadThumbnailsForRows(const QList<int>& rows) override;
    void migrateCache(const QString& oldPath, const QString& newPath) override;
    void clearCacheForFolder(const QString& folderPath) override;
    void flushPendingUpdates() override;

signals:
    void recordRenamed(const QString& oldPath, const QString& newPath, const QString& newName);

protected:
    bool isSuspended() const;

    std::vector<QuarkMeta::ItemRecord> m_allRecords;
    std::unordered_map<QString, int, QuarkMeta::QStringHash> m_pathToIndex;
    mutable QCache<QString, QIcon> m_iconCache;
    mutable QSet<QString> m_requestedIcons;
    mutable QMap<QString, double> m_aspectRatios;
    mutable QCache<QString, QuarkMeta::RuntimeMeta> m_metaCache;
    QString m_query;

    QSet<int> m_pendingUpdateRows;
    std::atomic<uint64_t> m_currentGen{0};
};

#endif // LIBRARYASSETMODEL_H
