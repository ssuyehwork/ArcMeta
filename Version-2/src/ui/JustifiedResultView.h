#pragma once
#include "IScanResultView.h"

namespace ArcMeta {

class DropJustifiedView;

class JustifiedResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit JustifiedResultView(DropJustifiedView* view, QWidget* parent = nullptr);
    ~JustifiedResultView() override;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropJustifiedView* m_view = nullptr;
};

} // namespace ArcMeta
