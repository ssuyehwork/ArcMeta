#ifndef LIBRARYASSETMODEL_H
#define LIBRARYASSETMODEL_H

#include "ItemModelBase.h"
#include <QCache>
#include <QMap>
#include <QIcon>

class LibraryAssetModel : public ItemModelBase {
    Q_OBJECT
public:
    explicit LibraryAssetModel(QObject* parent = nullptr);
    virtual ~LibraryAssetModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
    std::vector<ArcMeta::ItemRecord>& mutableRecords() { return m_allRecords; }

protected:
    std::vector<ArcMeta::ItemRecord> m_allRecords;
    mutable QCache<QString, QIcon> m_iconCache;
    mutable QMap<QString, double> m_aspectRatios;
};

#endif // LIBRARYASSETMODEL_H
