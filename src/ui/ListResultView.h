#pragma once
#include "IScanResultView.h"
#include "DropTreeView.h"

namespace ArcMeta {

class ListResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit ListResultView(DropTreeView* treeView, QObject* parent = nullptr);
    ~ListResultView() override = default;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropTreeView* m_treeView = nullptr;
};

} // namespace ArcMeta
