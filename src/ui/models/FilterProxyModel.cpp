#include "FilterProxyModel.h"
#include "../../core/ModelContract.h"

namespace ArcMeta {

FilterProxyModel::FilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void FilterProxyModel::updateFilter() {
    invalidateFilter();
}

bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    bool isDir = sourceModel()->data(idx, IsDirRole).toBool();
    QString filename = sourceModel()->data(idx, Qt::DisplayRole).toString();

    // 搜索词过滤
    if (!currentFilter.keyword.isEmpty()) {
        if (!filename.contains(currentFilter.keyword, Qt::CaseInsensitive)) {
            return false;
        }
    }

    // 文件夹/文件可见性过滤
    if (isDir) {
        if (!currentFilter.showFolders) return false;
    } else {
        if (!currentFilter.showFiles) return false;
    }

    // 评分过滤
    if (!currentFilter.ratings.isEmpty()) {
        int r = sourceModel()->data(idx, RatingRole).toInt();
        if (!currentFilter.ratings.contains(r)) return false;
    }

    // 设色过滤
    if (!currentFilter.colors.isEmpty()) {
        QString c = sourceModel()->data(idx, ColorRole).toString();
        if (!currentFilter.colors.contains(c.toUpper())) return false;
    }

    return true;
}

bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const {
    // 默认名称排序
    QString lStr = sourceModel()->data(source_left, Qt::DisplayRole).toString();
    QString rStr = sourceModel()->data(source_right, Qt::DisplayRole).toString();
    return lStr.localeAwareCompare(rStr) < 0;
}

} // namespace ArcMeta
