#pragma once

#include <QStandardItemModel>
#include <QSet>
#include "../core/ModelContract.h"

namespace ArcMeta {

class CategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    // 系统专属容器 ID：用于“分类”一级主分组标题
    static constexpr int CAT_GROUP_SYS_ID = -9;

    enum Type { System, User, Both };
    explicit CategoryModel(Type type, QObject* parent = nullptr);

    void setUnlockedIds(const QSet<int>& ids);
    void deferredRefresh();
    void loadCategoryItems(const QModelIndex& parentIndex);

public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
    void updateSystemCounts();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& val, int role = Qt::EditRole) override;

    Qt::DropActions supportedDropActions() const override;
    bool dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    Type m_type;
    QSet<int> m_unlockedIds;
    bool m_isFirstLoad = true;
};

} // namespace ArcMeta