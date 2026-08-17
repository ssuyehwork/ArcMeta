#include "JustifiedResultView.h"
#include "DropJustifiedView.h"

namespace ArcMeta {

JustifiedResultView::JustifiedResultView(DropJustifiedView* view, QWidget* parent) 
    : IScanResultView(parent), m_view(view) {
}

JustifiedResultView::~JustifiedResultView() {
}

QWidget* JustifiedResultView::getWidget() {
    return m_view;
}

QAbstractItemView* JustifiedResultView::getBaseView() {
    return m_view;
}

void JustifiedResultView::setModel(QAbstractItemModel* model) {
    m_view->setModel(model);
}

void JustifiedResultView::setIconSize(int size) {
    m_view->setTargetRowHeight(size);
}

void JustifiedResultView::refreshLayout() {
    m_view->setLayoutMode(JustifiedView::JustifiedMode);
    m_view->doItemsLayout();
}

} // namespace ArcMeta
