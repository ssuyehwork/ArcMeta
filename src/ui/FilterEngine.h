#pragma once

#include <QObject>
#include <QColor>
#include <QDate>
#include "FilterPanel.h"
#include "../core/ModelContract.h"
#include "../core/IndexedEntry.h"

namespace ArcMeta {

class FilterEngine {
public:
    static FilterEngine& instance();

    /**
     * @brief 判定单行是否匹配 FilterState
     */
    bool acceptsRow(const FilterState& currentFilter, const ItemRecord& record, const QString& fileName) const;

private:
    FilterEngine() = default;
    ~FilterEngine() = default;
    FilterEngine(const FilterEngine&) = delete;
    FilterEngine& operator=(const FilterEngine&) = delete;
};

} // namespace ArcMeta
