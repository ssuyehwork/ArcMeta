#pragma once
#include "IScanResultView.h"
#include "DropJustifiedView.h"

namespace ArcMeta {

class GridResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit GridResultView(DropJustifiedView* justifiedView, QObject* parent = nullptr);
    ~GridResultView() override = default;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropJustifiedView* m_justifiedView = nullptr;
};

} // namespace ArcMeta
