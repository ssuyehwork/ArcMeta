#ifndef ITEMMODELBASE_H
#define ITEMMODELBASE_H

#include <QAbstractTableModel>
#include <vector>
#include "ItemRecord.h" // 确保引用正确路径

class ItemModelBase : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ItemModelBase(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    virtual ~ItemModelBase() override = default;

    // 只暴露 allRecords() 接口，供 FilterProxyModel 统一操作
    virtual const std::vector<ItemRecord>& allRecords() const = 0;
};

#endif // ITEMMODELBASE_H
