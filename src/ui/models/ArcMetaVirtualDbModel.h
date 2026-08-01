#pragma once

#include <QAbstractTableModel>
#include <QCache>
#include <QIcon>
#include <QSet>
#include <QMap>
#include <vector>
#include <unordered_map>
#include "../../core/IndexedEntry.h"
#include "../../meta/MetadataDefs.h"

namespace ArcMeta {

struct QStringHash {
    size_t operator()(const QString& key) const {
        return qHash(key);
    }
};

class ArcMetaVirtualDbModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ArcMetaVirtualDbModel(QObject* parent = nullptr);
    ~ArcMetaVirtualDbModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void setQuery(const QString& query) { m_query = query; }
    void setRecords(const std::vector<ItemRecord>& records);
    void clear();

    const std::vector<ItemRecord>& allRecords() const { return m_allRecords; }
    void updateRecordMetadata(const QString& path);
    void loadThumbnailsForRows(const QList<int>& rows);
    void migrateCache(const QString& oldPath, const QString& newPath);
    void clearCacheForFolder(const QString& folderPath);

signals:
    void recordRenamed(const QString& oldPath, const QString& newPath, const QString& newName);

private:
    std::vector<ItemRecord> m_allRecords;
    std::unordered_map<QString, int, QStringHash> m_pathToIndex;
    int m_displayCount = 0;
    QString m_query;

    mutable QCache<QString, QIcon> m_iconCache;
    mutable QSet<QString> m_requestedIcons;
    mutable QMap<QString, double> m_aspectRatios;
    mutable QCache<QString, RuntimeMeta> m_metaCache;
};

} // namespace ArcMeta
