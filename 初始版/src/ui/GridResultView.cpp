#include "GridResultView.h"
#include "DropJustifiedView.h"

namespace ArcMeta {

GridResultView::GridResultView(DropJustifiedView* view, QWidget* parent) 
    : IScanResultView(parent), m_view(view) {
}

GridResultView::~GridResultView() {
}

QWidget* GridResultView::getWidget() {
    return m_view;
}

QAbstractItemView* GridResultView::getBaseView() {
    return m_view;
}

void GridResultView::setModel(QAbstractItemModel* model) {
    m_view->setModel(model);
}

void GridResultView::setIconSize(int size) {
    m_view->setTargetRowHeight(size);
}

void GridResultView::refreshLayout() {
    m_view->setLayoutMode(JustifiedView::GridMode);
    m_view->doItemsLayout();
}

} // namespace ArcMeta
