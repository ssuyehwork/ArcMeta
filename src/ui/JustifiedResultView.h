#pragma once
#include "IScanResultView.h"
#include "DropJustifiedView.h"

namespace ArcMeta {

class JustifiedResultView : public IScanResultView {
    Q_OBJECT
public:
    explicit JustifiedResultView(DropJustifiedView* justifiedView, QObject* parent = nullptr);
    ~JustifiedResultView() override = default;

    QWidget* getWidget() override;
    QAbstractItemView* getBaseView() override;
    void setModel(QAbstractItemModel* model) override;
    void setIconSize(int size) override;
    void refreshLayout() override;

private:
    DropJustifiedView* m_justifiedView = nullptr;
};

} // namespace ArcMeta
