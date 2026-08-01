#pragma once

#include <QSortFilterProxyModel>
#include "../../meta/MetadataDefs.h"

namespace ArcMeta {

class FilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject* parent = nullptr);

    FilterState currentFilter;
    void updateFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;
};

} // namespace ArcMeta
