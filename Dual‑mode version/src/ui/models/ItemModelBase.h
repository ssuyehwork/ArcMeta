#ifndef ITEMMODELBASE_H
#define ITEMMODELBASE_H

#include <QAbstractTableModel>
#include <vector>
#include <QHash>
#include "src/core/ItemRecord.h" // 修正为正确的头文件路径

namespace ArcMeta {
    struct QStringHash {
        size_t operator()(const QString& key) const {
            return qHash(key);
        }
    };
}

class ItemModelBase : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ItemModelBase(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    virtual ~ItemModelBase() override = default;

    // 暴露通用接口合约，由 DiskItemModel 和 LibraryAssetModel 多态实现
    virtual const std::vector<ArcMeta::ItemRecord>& allRecords() const = 0;
    virtual void setRecords(const std::vector<ArcMeta::ItemRecord>& records) = 0;
    virtual void clear() = 0;
    virtual void setQuery(const QString& query) = 0;
    virtual void updateRecordMetadata(const QString& path) = 0;
    virtual void loadThumbnailsForRows(const QList<int>& rows) = 0;
    virtual void migrateCache(const QString& oldPath, const QString& newPath) = 0;
    virtual void clearCacheForFolder(const QString& folderPath) = 0;
    virtual void flushPendingUpdates() {}
};

#endif // ITEMMODELBASE_H
