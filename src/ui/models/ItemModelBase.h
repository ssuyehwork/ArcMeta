#ifndef ITEMMODELBASE_H
#define ITEMMODELBASE_H

#include <QAbstractTableModel>
#include <vector>
#include <QHash>
#include <QMimeData>
#include <QUrl>
#include "src/core/ItemRecord.h" // 修正为正确的头文件路径
#include "src/core/ModelContract.h"

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

    // 返回项特征标志，包含可选择、使能、拖拽及特定编辑
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        if (!index.isValid()) return Qt::NoItemFlags;
        Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
        if (index.column() == 0) {
            const auto& records = allRecords();
            if (index.row() < static_cast<int>(records.size()) && !records[index.row()].isCategory) {
                f |= Qt::ItemIsEditable;
            }
        }
        return f;
    }

    // 支持拖拽的核心虚函数接口实现 (多态向下分发)
    QStringList mimeTypes() const override {
        return { "text/uri-list" };
    }

    QMimeData* mimeData(const QModelIndexList& indexes) const override {
        QMimeData* mime = new QMimeData();
        QList<QUrl> urls;
        for (const auto& idx : indexes) {
            if (idx.column() == 0) {
                QString path = idx.data(ArcMeta::PathRole).toString();
                if (!path.isEmpty()) {
                    urls << QUrl::fromLocalFile(path);
                }
            }
        }
        if (urls.isEmpty()) {
            delete mime;
            return nullptr;
        }
        mime->setUrls(urls);
        return mime;
    }

    Qt::DropActions supportedDragActions() const override {
        return Qt::CopyAction | Qt::MoveAction;
    }
};

#endif // ITEMMODELBASE_H
