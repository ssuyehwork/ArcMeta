#pragma once
#include "IScanResultView.h"

namespace QuarkMeta {

class DropJustifiedView;

class GridResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit GridResultView(DropJustifiedView* view, QWidget* parent = nullptr);
    ~GridResultView() override;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropJustifiedView* m_view = nullptr;
};

} // namespace QuarkMeta
