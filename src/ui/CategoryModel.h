#pragma once

#include <QStandardItemModel>
#include <QSet>
#include <QMimeData>
#include "../core/ModelContract.h"
#include "../meta/CategoryRepo.h"

namespace QuarkMeta {

class CategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    static constexpr int CAT_GROUP_SYS_ID = -9;

    enum Type { System, User, Both };
    explicit CategoryModel(Type type, QObject* parent = nullptr);

    void setUnlockedIds(const QSet<int>& ids);
    void deferredRefresh();
    void loadCategoryItems(const QModelIndex& parentIndex);

signals:
    // 🚀 【重构解耦】：通知外部中介控制器执行改名与排序写库 
    void categoryRenameRequested(int catId, const QString& newName); 
    void categoryOrderChanged(int draggedId, int targetParentId, int insertRow); 

public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
    void updateStatisticsWithSnapshot(const StatisticsSnapshot& snapshot);
    void updateItemCounts(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts) {
        updateStatistics(sysCounts, catCounts);
    }
    void updateSystemCounts();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& val, int role = Qt::EditRole) override;

    // 拖拽自定义 MimeData 打包与精准接收
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    Type m_type;
    QSet<int> m_unlockedIds;
    bool m_isFirstLoad = true;
};

} // namespace QuarkMeta