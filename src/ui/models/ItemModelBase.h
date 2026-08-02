#ifndef ITEMMODELBASE_H
#define ITEMMODELBASE_H

#include <QAbstractTableModel>
#include <vector>
#include "src/core/ItemRecord.h" // 修正为正确的头文件路径

class ItemModelBase : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ItemModelBase(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    virtual ~ItemModelBase() override = default;

    // 只暴露 allRecords() 接口，供 FilterProxyModel 统一操作
    virtual const std::vector<ArcMeta::ItemRecord>& allRecords() const = 0;
};

#endif // ITEMMODELBASE_H
