#pragma once
#include "IScanResultView.h"

namespace ArcMeta {

class DropTreeView;

class ListResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit ListResultView(DropTreeView* treeView, QWidget* parent = nullptr);
    ~ListResultView() override;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropTreeView* m_treeView = nullptr;
};

} // namespace ArcMeta
